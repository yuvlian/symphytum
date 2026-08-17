$odin = Get-Command odin.exe -ErrorAction Stop
$odinDir = Split-Path $odin.Source
$sharedDir = Join-Path $odinDir "shared"
$repoDir = Join-Path $sharedDir "il2cure"

git clone --branch main --single-branch --depth 1 "https://github.com/yuvlian/il2cure" $repoDir
git -C $repoDir fetch --depth 1 origin 2eff70db29a106919934784933d2205b1c3b165c
git -C $repoDir checkout --detach 2eff70db29a106919934784933d2205b1c3b165c
