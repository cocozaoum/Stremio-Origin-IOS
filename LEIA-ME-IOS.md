# Stremio Origin → iOS (IPA para SideStore)

Pacote de port (não testado em iPhone: foi escrito sem acesso a Mac/Xcode). Ele **se sobrepõe** ao código-fonte `android-phone-1.1.0`.

## Passo a passo (tudo pelo navegador + GitHub)

1. Crie uma conta no GitHub e um repositório **público** novo (macOS runners são grátis e ilimitados em repositório público).
2. Suba o conteúdo de `android-phone-1.1.0/` (a pasta que está dentro do .zip do dev) na raiz do repositório.
3. Suba por cima, na mesma raiz, as pastas deste pacote: `src/`, `tools/ios/` e `.github/workflows/ios.yml`.
   (No site: "Add file → Upload files". A pasta `.github` é oculta no Mac/Windows: ative "mostrar ocultos" ou use o GitHub Desktop.)
4. Aba **Actions** → "Build iOS IPA" → **Run workflow**. Leva ~20–40 min na primeira vez.
5. Ao terminar, baixe o artefato **ios-build** (contém `StremioOrigin.ipa` e `build.log`).
6. Mande o IPA para o iPhone e instale pelo SideStore (My Apps → "+" → escolha o .ipa).

**Se o build falhar** (é esperado nas primeiras tentativas): baixe o `build.log` (ou copie as ~30 últimas linhas com "error:") e me mande. Cada ciclo costuma resolver um grupo de erros.

## Chaves TMDB/Trakt (opcional)
O dev não publica as chaves dele. Sem elas o app funciona, mas cada serviço pede a sua chave. Para embutir as suas: no repositório, Settings → Secrets and variables → Actions → crie `TMDB_API_KEY` (e `TRAKT_CLIENT_ID`).

## O que esta primeira versão faz
- Interface completa (é o mesmo código C do Android), conta Stremio, addons, catálogos, buscas.
- Reprodução dentro do app com AVPlayer: **MP4, MOV e HLS** (H.264/HEVC, AAC/AC-3/E-AC-3).
- Abrir o link em VLC, Infuse ou OutPlayer (se instalados).

## O que ainda NÃO existe (próximas etapas)
1. **MKV, DTS, TrueHD**: precisa trocar o AVPlayer por VLCKit (libVLC para iOS), que é o que o Android usa.
2. **Torrents** (motor `libsbengine`, C++/libtorrent) e **downloader** (`libsbdl`, Go): precisam ser compilados para iOS. Sem eles o app avisa "no engine library" e segue funcionando para links diretos/debrid.
3. Legendas embutidas no arquivo, PiP, notificações de download, controles na tela de bloqueio, Chromecast/Android TV remote.
4. Ajustes de layout (entalhe/Dynamic Island, paisagem) só aparecem testando no aparelho.
