$repos = @{
    "https://github.com/yuvlian/il2cure" = @{
        branch = "main"
        commit = "e9c2da92e53ab64fdfa036726f9684c1e8d8ab9c"
    }
}

$odin = Get-Command odin.exe -ErrorAction Stop
$odinDir = Split-Path $odin.Source
$sharedDir = Join-Path $odinDir "shared"

foreach ($repo in $repos.GetEnumerator()) {
    $repoUrl = $repo.Key
    $branch = $repo.Value.branch
    $commit = $repo.Value.commit

    $repoName = ($repoUrl.TrimEnd("/") -split "/")[-1]
    $shortCommit = $commit.Substring(0, 7)

    $repoDir = Join-Path $sharedDir $repoName
    $commitDir = Join-Path $repoDir $shortCommit

    if (Test-Path $commitDir) {
        Write-Host "$repoName @ $shortCommit already exists, skipping."
        continue
    }

    New-Item -ItemType Directory -Force -Path $repoDir | Out-Null

    Write-Host "Cloning $repoName [$branch] @ $shortCommit"

    git clone `
        --branch $branch `
        --single-branch `
        --depth 1 `
        $repoUrl `
        $commitDir

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to clone $repoUrl"
        continue
    }

    git -C $commitDir fetch --depth 1 origin $commit

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to fetch commit $commit"
        continue
    }

    git -C $commitDir checkout --detach $commit
}
