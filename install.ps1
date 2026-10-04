<#
.SYNOPSIS
  Installs GrandTheftMinecraft (and ScriptHookV + its ASI loader) into a GTA V Legacy folder, or removes it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1
  powershell -ExecutionPolicy Bypass -File install.ps1 -GameDir "E:\Games\GTA Modding"
  powershell -ExecutionPolicy Bypass -File install.ps1 -Remove

  Install copies:  ScriptHookV.dll, dinput8.dll (ASI loader), args.txt (-nobattleye), GrandTheftMinecraft.asi and
  GrandTheftMinecraft\defs.txt + dlc_tex.txt (the mod builds everything Minecraft from them on first launch).
  Your world.txt and config.ini are kept when updating. This is the developer install; players use the .oiv.
  Every file it adds is listed in GrandTheftMinecraft\install-manifest.txt; -Remove deletes exactly those and
  keeps a copy of your world in _gtm_backup\.
#>
param(
	[string]$GameDir = "D:\SteamLibrary\steamapps\common\GTA Modding",
	[switch]$Remove,
	[switch]$NoDlc  # skip Stage 2 (OpenIV.asi + the textured block DLC); blocks then use Stage 1 polygons
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Data = Join-Path $GameDir "GrandTheftMinecraft"
$Manifest = Join-Path $Data "install-manifest.txt"
$Backup = Join-Path $GameDir "_gtm_backup"
$Pack = Join-Path $Repo "tools\gtmpack\bin\Release\net8.0-windows\gtmpack.exe"
$ModsUpdate = Join-Path $GameDir "mods\update\update.rpf"

if (-not (Test-Path (Join-Path $GameDir "GTA5.exe"))) { throw "GTA5.exe not found in $GameDir (pass -GameDir)" }
if ((Split-Path -Leaf $GameDir) -eq "Grand Theft Auto V") {
	Write-Warning "This looks like Steam's own GTA V folder. The mod is meant for a separate modding copy."
	$ok = Read-Host "Install here anyway? (y/N)"
	if ($ok -ne "y") { exit 1 }
}

if ($Remove) {
	if (-not (Test-Path $Manifest)) { throw "No install manifest at $Manifest; nothing to remove." }
	New-Item -ItemType Directory -Force $Backup | Out-Null
	foreach ($keep in "world.txt", "config.ini") {
		$f = Join-Path $Data $keep
		if (Test-Path $f) { Copy-Item $f (Join-Path $Backup $keep) -Force }
	}
	$listed = Get-Content $Manifest
	if ((Test-Path $ModsUpdate) -and -not ($listed -contains "mods\update\update.rpf") -and (Test-Path $Pack)) {
		& $Pack dlclist $GameDir --remove
	}
	foreach ($rel in $listed) {
		$p = Join-Path $GameDir $rel
		if (Test-Path $p) { Remove-Item $p -Force -Recurse; Write-Host "removed $rel" }
	}
	if (Test-Path $Data) { Remove-Item $Data -Recurse -Force; Write-Host "removed GrandTheftMinecraft\" }
	Write-Host "Done. Your world and config were copied to $Backup"
	exit 0
}

$Asi = Join-Path $Repo "build\GrandTheftMinecraft.asi"
$Runtime = Join-Path $Repo "third_party\runtime"
if (-not (Test-Path $Asi)) { throw "Build first: $Asi is missing (run build.sh)" }
$Layout = Join-Path $Repo "build\dlc\gtm\dlc_tex.txt"
if (-not (Test-Path $Layout)) { throw "Build the block pack first (tools\make_dlc_src.py, gtmpack build)" }

$added = New-Object System.Collections.Generic.List[string]
if (Test-Path $Manifest) { $added.AddRange([string[]](Get-Content $Manifest)) }
function Track($rel) { if (-not $added.Contains($rel)) { $added.Add($rel) } }

# ScriptHookV + Legacy ASI loader (dinput8.dll). Never the zip's xinput1_4.dll: that's the Enhanced loader.
foreach ($f in "ScriptHookV.dll", "dinput8.dll") {
	$dst = Join-Path $GameDir $f
	if (Test-Path $dst) {
		$src = Join-Path $Runtime $f
		$same = (Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash
		if ($added.Contains($f)) { if (-not $same) { Copy-Item $src $dst -Force; Write-Host "updated $f" } }
		else { Write-Host "keeping your existing $f" }
	} else {
		Copy-Item (Join-Path $Runtime $f) $dst
		Track $f
		Write-Host "installed $f"
	}
}
# BattlEye off for this copy only (story mode; GTA Online must never be used with mods)
$argsFile = Join-Path $GameDir "args.txt"
if (Test-Path $argsFile) {
	$cur = Get-Content $argsFile -Raw
	if ($cur -notmatch "-nobattleye") { Write-Warning "args.txt exists without -nobattleye; add it yourself." }
} else {
	Set-Content -Path $argsFile -Value "-nobattleye" -Encoding ASCII
	Track "args.txt"
	Write-Host "created args.txt (-nobattleye)"
}

Copy-Item $Asi (Join-Path $GameDir "GrandTheftMinecraft.asi") -Force
Track "GrandTheftMinecraft.asi"
Write-Host "installed GrandTheftMinecraft.asi"

New-Item -ItemType Directory -Force $Data | Out-Null
Copy-Item (Join-Path $Repo "data\defs.txt") (Join-Path $Data "defs.txt") -Force
Copy-Item $Layout (Join-Path $Data "dlc_tex.txt") -Force
Copy-Item (Join-Path $Repo "build\dlc_src\rigs.txt") (Join-Path $Data "rigs.txt") -Force
# a new block pack has blank textures again: forget the old fill so the mod rebuilds it
Remove-Item (Join-Path $Data "dlc_ready.txt") -ErrorAction SilentlyContinue
Write-Host "installed GrandTheftMinecraft\ defs (the mod builds the Minecraft assets on first launch)"

# ---- Stage 2: textured block props as an add-on DLC, loaded through OpenIV's mods folder ----
if (-not $NoDlc) {
	$Dlc = Join-Path $Repo "build\dlc\gtm\dlc.rpf"
	if (-not (Test-Path $Dlc)) { throw "Build the DLC first: tools\gtmpack build (see README)" }
	# OpenIV.asi must come from OpenIV's own ASI Manager: the copy in OpenIV's app folder is a packed container,
	# not a DLL (a plain copy fails with "OpenIV.asi failed to load" in asiloader.log)
	$dstAsi = Join-Path $GameDir "OpenIV.asi"
	$isDll = (Test-Path $dstAsi) -and ([System.IO.File]::ReadAllBytes($dstAsi)[0..1] -join ",") -eq "77,90"
	if (-not $isDll) {
		Write-Warning ("OpenIV.asi is not installed in $GameDir. In OpenIV: Tools > ASI Manager, pick this GTA folder, " +
			"and press Install next to 'OpenIV.asi' (not 'ASI Loader'). Without it the block pack won't load and " +
			"blocks fall back to Stage 1 polygons.")
	}
	$dlcDir = Join-Path $GameDir "mods\update\x64\dlcpacks\gtm"
	New-Item -ItemType Directory -Force $dlcDir | Out-Null
	Copy-Item $Dlc (Join-Path $dlcDir "dlc.rpf") -Force
	Track "mods\update\x64\dlcpacks\gtm"
	Write-Host "installed mods\update\x64\dlcpacks\gtm\dlc.rpf"
	if (-not (Test-Path $ModsUpdate)) { Track "mods\update\update.rpf" }
	& $Pack dlclist $GameDir
	if ($LASTEXITCODE -ne 0) { throw "registering the DLC in mods\update\update.rpf failed" }
}
$added | Set-Content -Path $Manifest -Encoding UTF8
Write-Host "Done. Launch GTA from this folder (PlayGTAV.exe), Story Mode only. F6 toggles Minecraft mode."
