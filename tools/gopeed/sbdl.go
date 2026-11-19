// libsbdl: Gopeed's downloader (github.com/GopeedLab/gopeed, GPL-3.0) behind the C interface in src/phone/sbdl.h.
// HTTP(S) only. build.sh copies this file and sbdl_table.c into the Gopeed source tree (they use its internal
// packages) and builds a c-shared library.
//
// Every C call returns at once: changes go to one worker goroutine that runs them in order, and stat() reads a
// table that a second goroutine refreshes from the engine five times a second. Our ids are short random strings
// kept in the task's request labels, so they are known before the engine has made the task and stay the same
// across restarts.
package main

/*
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sbdl.h"
*/
import "C"

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unsafe"

	ihttp "github.com/GopeedLab/gopeed/internal/protocol/http"
	"github.com/GopeedLab/gopeed/internal/fetcher"
	"github.com/GopeedLab/gopeed/pkg/base"
	"github.com/GopeedLab/gopeed/pkg/download"
	phttp "github.com/GopeedLab/gopeed/pkg/protocol/http"
)

const labelKey = "sbdl"

// pinned Gopeed commit, set by build.sh
var gopeedCommit = "unknown"

type entry struct {
	state       int
	total, done int64
	speed       int64
	conns       int
	name, path  string
	err         string
	eid         string // engine task id
	override    int // a state set by a queued pause/resume until the worker has run it; -2 = none
}

var (
	mu       sync.Mutex
	started  bool
	setupErr string
	dl       *download.Downloader
	conns    = 0
	storage  string
	table    = map[string]*entry{}  // our id -> state
	engineID = map[string]string{}  // our id -> engine task id
	removed  = map[string]bool{}    // removed by the app, kept out of the table
	errs     = map[string]string{}  // engine id -> last error text
	paused   = map[string]bool{}    // our ids the user paused (kept paused across restarts)

	qmu   sync.Mutex
	queue []func()
	qwake = make(chan struct{}, 1)

	versionStr *C.char
)

func newID() string {
	b := make([]byte, 8)
	rand.Read(b)
	return hex.EncodeToString(b)
}

func enqueue(f func()) {
	qmu.Lock()
	queue = append(queue, f)
	qmu.Unlock()
	select {
	case qwake <- struct{}{}:
	default:
	}
}

func worker() {
	for {
		qmu.Lock()
		if len(queue) == 0 {
			qmu.Unlock()
			<-qwake
			continue
		}
		f := queue[0]
		queue = queue[1:]
		qmu.Unlock()
		func() {
			defer func() { recover() }()
			f()
		}()
	}
}

func pausedFile() string { return filepath.Join(storage, "sbdl-paused.json") }

func savePaused() { // mu held
	ids := make([]string, 0, len(paused))
	for id := range paused {
		ids = append(ids, id)
	}
	b, _ := json.Marshal(ids)
	tmp := pausedFile() + ".tmp"
	if os.WriteFile(tmp, b, 0600) == nil {
		os.Rename(tmp, pausedFile())
	}
}

func aliasOf(t *download.Task) string {
	if t.Meta != nil && t.Meta.Req != nil && t.Meta.Req.Labels != nil {
		if a := t.Meta.Req.Labels[labelKey]; a != "" {
			return a
		}
	}
	return t.ID
}

func setup() (err error) {
	defer func() {
		if r := recover(); r != nil {
			err = fmt.Errorf("%v", r)
		}
	}()
	if b, e := os.ReadFile(pausedFile()); e == nil {
		var ids []string
		if json.Unmarshal(b, &ids) == nil {
			mu.Lock()
			for _, id := range ids {
				paused[id] = true
			}
			mu.Unlock()
		}
	}
	d := download.NewDownloader(&download.DownloaderConfig{
		FetchManagers:  []fetcher.FetcherManager{new(ihttp.FetcherManager)},
		Storage:        download.NewBoltStorage(storage),
		StorageDir:     storage,
		TempDir:        filepath.Join(storage, "tmp"),
		ProductionMode: true, // logs to storageDir/logs, not stdout
	})
	if err = d.Setup(); err != nil {
		return err
	}
	d.Listener(func(ev *download.Event) {
		if ev.Key == download.EventKeyError && ev.Task != nil && ev.Err != nil {
			mu.Lock()
			errs[ev.Task.ID] = ev.Err.Error()
			mu.Unlock()
		}
	})
	mu.Lock()
	dl = d
	var resume []string
	for _, t := range d.GetTasks() {
		a := aliasOf(t)
		engineID[a] = t.ID
		if t.Status == base.DownloadStatusPause && !paused[a] {
			resume = append(resume, t.ID)
		}
	}
	mu.Unlock()
	if len(resume) > 0 {
		d.Continue(&download.TaskFilter{IDs: resume})
	}
	refresh()
	go refresher()
	return nil
}

func mapStatus(s base.Status) int {
	switch s {
	case base.DownloadStatusRunning:
		return C.SBDL_RUNNING
	case base.DownloadStatusPause:
		return C.SBDL_PAUSED
	case base.DownloadStatusError:
		return C.SBDL_ERROR
	case base.DownloadStatusDone:
		return C.SBDL_DONE
	}
	return C.SBDL_WAIT
}

// open connections of a running HTTP task; Stats takes the task's lock, so it runs off the refresh loop
var connBusy sync.Map

func countConns(d *download.Downloader, id string, alias string) {
	if _, busy := connBusy.LoadOrStore(id, true); busy {
		return
	}
	go func() {
		defer connBusy.Delete(id)
		defer func() { recover() }()
		sr, err := d.Stats(id)
		if err != nil || sr == nil {
			return
		}
		st, ok := sr.(*fetcher.Stats)
		if !ok || st == nil || st.Snapshot == nil {
			return
		}
		b, err := json.Marshal(st.Snapshot)
		if err != nil {
			return
		}
		var hs phttp.Stats
		if json.Unmarshal(b, &hs) != nil {
			return
		}
		n := 0
		for _, c := range hs.Connections {
			if c != nil && !c.Completed && !c.Failed {
				n++
			}
		}
		mu.Lock()
		if e := table[alias]; e != nil && e.state == C.SBDL_RUNNING {
			e.conns = n
		}
		mu.Unlock()
	}()
}

func refresh() {
	defer func() { recover() }()
	d := dl
	if d == nil {
		return
	}
	tasks := d.GetTasks()
	seen := make(map[string]bool, len(tasks))
	for _, t := range tasks {
		if t == nil || t.Meta == nil {
			continue
		}
		a := aliasOf(t)
		e := entry{override: -2, state: mapStatus(t.Status), eid: t.ID}
		if t.Meta.Res != nil {
			e.total = t.Meta.Res.Size
			if len(t.Meta.Res.Files) > 0 && t.Meta.Opts != nil {
				e.path = t.Meta.SingleFilepath()
			}
		}
		if t.Progress != nil {
			e.done = t.Progress.Downloaded
			if e.state == C.SBDL_RUNNING {
				e.speed = t.Progress.Speed
			}
		}
		if t.Meta.Opts != nil && t.Meta.Req != nil {
			e.name = t.Name()
		}
		if e.state == C.SBDL_DONE && e.total > 0 {
			e.done = e.total
		}
		seen[a] = true
		mu.Lock()
		if removed[a] {
			mu.Unlock()
			continue
		}
		engineID[a] = t.ID
		if old := table[a]; old != nil {
			e.override = old.override
			if e.state == C.SBDL_RUNNING {
				e.conns = old.conns
			}
		}
		table[a] = &e
		mu.Unlock()
		if e.state == C.SBDL_RUNNING && t.Meta.Res != nil {
			countConns(d, t.ID, a)
		}
	}
	// a removed task the engine no longer has can be forgotten
	mu.Lock()
	for a := range removed {
		if !seen[a] {
			if _, still := engineID[a]; !still {
				delete(removed, a)
			}
		}
	}
	mu.Unlock()
}

func refresher() {
	for {
		time.Sleep(200 * time.Millisecond)
		refresh()
	}
}

func gs(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}

func parseHeaders(s string) map[string]string {
	h := map[string]string{}
	for _, line := range strings.Split(s, "\n") {
		line = strings.TrimRight(line, "\r")
		i := strings.IndexByte(line, ':')
		if i <= 0 {
			continue
		}
		k := strings.TrimSpace(line[:i])
		v := strings.TrimSpace(line[i+1:])
		if k != "" {
			h[k] = v
		}
	}
	return h
}

// a command on a task once the engine knows it (the worker runs in order, so a queued add has already run)
func withEngine(alias string, f func(d *download.Downloader, id string) error) {
	enqueue(func() {
		mu.Lock()
		d, id := dl, engineID[alias]
		mu.Unlock()
		var err error
		if d == nil {
			err = errors.New("not started")
		} else if id == "" {
			err = download.ErrTaskNotFound
		} else {
			err = f(d, id)
		}
		_ = err
		refresh()
		mu.Lock()
		if e := table[alias]; e != nil {
			e.override = -2
		}
		mu.Unlock()
	})
}

//export sbdlApi
func sbdlApi() C.int { return C.SBDL_API_VERSION }

//export sbdlVersion
func sbdlVersion() *C.char {
	mu.Lock()
	defer mu.Unlock()
	if versionStr == nil {
		versionStr = C.CString("gopeed " + gopeedCommit + " (http)")
	}
	return versionStr
}

//export sbdlStart
func sbdlStart(dir *C.char, nconn C.int) C.int {
	mu.Lock()
	defer mu.Unlock()
	if started { // started already: only the connections for new downloads change (the app's download mode)
		if nconn > 0 {
			conns = int(nconn)
		}
		return 0
	}
	storage = gs(dir)
	if storage == "" {
		return -1
	}
	if err := os.MkdirAll(storage, 0755); err != nil {
		return -1
	}
	conns = int(nconn)
	if conns <= 0 {
		conns = 8
	}
	started = true
	go worker()
	enqueue(func() {
		if err := setup(); err != nil {
			mu.Lock()
			setupErr = err.Error()
			mu.Unlock()
		}
	})
	return 0
}

//export sbdlAdd
func sbdlAdd(curl, cdir, cname, cheaders *C.char, idOut *C.char, idCap C.int) C.int {
	url, dir, name, headers := gs(curl), gs(cdir), gs(cname), gs(cheaders)
	mu.Lock()
	ok := started
	mu.Unlock()
	if !ok || url == "" || idOut == nil || idCap < 17 {
		return -1
	}
	alias := newID()
	shown := name
	if shown == "" {
		shown = filepath.Base(strings.SplitN(strings.SplitN(url, "?", 2)[0], "#", 2)[0])
	}
	mu.Lock()
	table[alias] = &entry{state: C.SBDL_WAIT, name: shown, override: -2}
	n := conns
	mu.Unlock()
	enqueue(func() {
		mu.Lock()
		d, se := dl, setupErr
		mu.Unlock()
		var err error
		var id string
		if d == nil {
			err = errors.New("engine did not start: " + se)
		} else {
			req := &base.Request{URL: url, Labels: map[string]string{labelKey: alias}}
			if h := parseHeaders(headers); len(h) > 0 {
				req.Extra = &phttp.ReqExtra{Header: h}
			}
			opts := &base.Options{Path: dir, Name: name, SkipAutoCategorize: true, Extra: &phttp.OptsExtra{Connections: n}}
			id, err = d.CreateDirect(req, opts)
		}
		mu.Lock()
		if removed[alias] {
			mu.Unlock()
			if err == nil {
				d.Delete(&download.TaskFilter{IDs: []string{id}}, true)
				mu.Lock()
				delete(removed, alias)
				mu.Unlock()
			}
			return
		}
		if err != nil {
			if e := table[alias]; e != nil {
				e.state, e.err = C.SBDL_ERROR, err.Error()
			}
			mu.Unlock()
			return
		}
		engineID[alias] = id
		mu.Unlock()
		refresh()
	})
	cs := C.CString(alias)
	C.strncpy(idOut, cs, C.size_t(idCap-1))
	*(*C.char)(unsafe.Add(unsafe.Pointer(idOut), int(idCap-1))) = 0
	C.free(unsafe.Pointer(cs))
	return 0
}

func setOverride(alias string, state int, userPaused bool) bool {
	mu.Lock()
	defer mu.Unlock()
	e := table[alias]
	if e == nil || removed[alias] {
		return false
	}
	e.override = state
	if userPaused {
		paused[alias] = true
	} else {
		delete(paused, alias)
	}
	if storage != "" {
		savePaused()
	}
	return true
}

//export sbdlPause
func sbdlPause(cid *C.char) C.int {
	alias := gs(cid)
	if !setOverride(alias, C.SBDL_PAUSED, true) {
		return -1
	}
	withEngine(alias, func(d *download.Downloader, id string) error {
		return d.Pause(&download.TaskFilter{IDs: []string{id}})
	})
	return 0
}

//export sbdlResume
func sbdlResume(cid *C.char) C.int {
	alias := gs(cid)
	if !setOverride(alias, C.SBDL_RUNNING, false) {
		return -1
	}
	withEngine(alias, func(d *download.Downloader, id string) error {
		return d.Continue(&download.TaskFilter{IDs: []string{id}})
	})
	return 0
}

//export sbdlRemove
func sbdlRemove(cid *C.char, deleteFile C.int) C.int {
	alias := gs(cid)
	mu.Lock()
	if table[alias] == nil {
		mu.Unlock()
		return -1
	}
	delete(table, alias)
	removed[alias] = true
	if paused[alias] {
		delete(paused, alias)
		savePaused()
	}
	mu.Unlock()
	force := deleteFile != 0
	enqueue(func() {
		mu.Lock()
		d, id := dl, engineID[alias]
		mu.Unlock()
		if d != nil && id != "" {
			d.Delete(&download.TaskFilter{IDs: []string{id}}, force)
		}
		mu.Lock()
		delete(engineID, alias)
		delete(errs, id)
		mu.Unlock()
		// removed[alias] is cleared by refresh once the engine has let the task go
	})
	return 0
}

func fill(dst *C.char, cap int, s string) {
	b := []byte(s)
	if len(b) > cap-1 {
		b = b[:cap-1]
	}
	if len(b) > 0 {
		C.memcpy(unsafe.Pointer(dst), unsafe.Pointer(&b[0]), C.size_t(len(b)))
	}
	*(*C.char)(unsafe.Add(unsafe.Pointer(dst), len(b))) = 0
}

//export sbdlStat
func sbdlStat(cid *C.char, out *C.sbdl_stat) C.int {
	alias := gs(cid)
	mu.Lock()
	e := table[alias]
	if e == nil || out == nil {
		mu.Unlock()
		return -1
	}
	v := *e
	if v.eid != "" && errs[v.eid] != "" {
		v.err = errs[v.eid] // the error event can land after the refresh that saw the state
	}
	mu.Unlock()
	if v.override != -2 && v.state != C.SBDL_DONE && v.state != C.SBDL_ERROR {
		v.state = v.override
	}
	if v.state != C.SBDL_RUNNING {
		v.speed, v.conns = 0, 0
	}
	out.state = C.int(v.state)
	out.total = C.int64_t(v.total)
	out.done = C.int64_t(v.done)
	out.speed = C.int64_t(v.speed)
	out.connections = C.int(v.conns)
	fill(&out.name[0], len(out.name), v.name)
	fill(&out.path[0], len(out.path), v.path)
	errText := ""
	if v.state == C.SBDL_ERROR {
		errText = v.err
		if errText == "" {
			errText = "failed"
		}
	}
	fill(&out.err[0], len(out.err), errText)
	return 0
}

func main() {}
