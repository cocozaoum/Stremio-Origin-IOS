package download

// libsbdl build (tools/gopeed/build.sh): Gopeed's extensions (JavaScript, webview, git) and archive extraction are
// left out, so the HTTP downloader does not carry their dependencies. Everything here is a no-op.

import (
	"errors"

	"github.com/GopeedLab/gopeed/pkg/base"
)

type Extension struct{}

type extensionWebViewProfile struct{}

type ArchivePartInfo struct {
	IsMultiPart   bool
	BaseName      string
	PartNumber    int
	FirstPartPath string
	Pattern       string
}

type ExtractProgressCallback func(extractedFiles int, totalFiles int, progress int)

var errNoExtract = errors.New("archive extraction is not built in")

func (d *Downloader) triggerOnResolve(req *base.Request) (*base.Resource, error) { return nil, nil }
func (d *Downloader) triggerOnStart(task *Task)                                  {}
func (d *Downloader) triggerOnError(task *Task, err error)                       {}
func (d *Downloader) triggerOnDone(task *Task)                                   {}
func (d *Downloader) waitForExtensionGit()                                       {}

func isArchiveFile(filename string) bool                 { return false }
func isFirstPart(filename string) bool                   { return false }
func getArchivePartInfo(filename string) ArchivePartInfo { return ArchivePartInfo{} }
func GetMultiPartArchiveBaseName(filename string) string { return filename }
func extractArchive(archivePath, destDir, password string, cb ExtractProgressCallback) error {
	return errNoExtract
}
func extractMultiPartArchive(firstPartPath, destDir, password string, cb ExtractProgressCallback) error {
	return errNoExtract
}
