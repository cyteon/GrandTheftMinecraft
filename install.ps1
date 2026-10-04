<#
.SYNOPSIS
  Installs GrandTheftMinecraft (and ScriptHookV + its ASI loader) into a GTA V Legacy folder, or removes it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1
  powershell -ExecutionPolicy Bypass -File install.ps1 -GameDir "E:\Games\GTA Modding"
  powershell -ExecutionPolicy Bypass -File install.ps1 -Remove

  Install copies:  ScriptHookV.dll, dinput8.dll (ASI loader), args.txt (-nobattleye), GrandTheftMinecraft.asi and
  the GrandTheftMinecraft\ data folder. Your world.txt and config.ini are kept when updating.
  Every file it adds is listed in GrandTheftMinecraft\install-manifest.txt; -Remove deletes exactly those and
  keeps a copy of your world in _gtm_backup\.
#>
param(
	[string]$GameDir = "D:\SteamLibrary\steamapps\common\GTA Modding",
	[switch]$Remove
)
$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Data = Join-Path $GameDir "GrandTheftMinecraft"
$Manifest = Join-Path $Data "install-manifest.txt"
$Backup = Join-Path $GameDir "_gtm_backup"

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
	foreach ($rel in Get-Content $Manifest) {
		$p = Join-Path $GameDir $rel
		if (Test-Path $p) { Remove-Item $p -Force -Recurse; Write-Host "removed $rel" }
	}
	if (Test-Path $Data) { Remove-Item $Data -Recurse -Force; Write-Host "removed GrandTheftMinecraft\" }
	Write-Host "Done. Your world and config were copied to $Backup"
	exit 0
}

$Asi = Join-Path $Repo "build\GrandTheftMinecraft.asi"
$BuiltData = Join-Path $Repo "build\GrandTheftMinecraft"
$Runtime = Join-Path $Repo "third_party\runtime"
if (-not (Test-Path $Asi)) { throw "Build first: $Asi is missing (run build.sh)" }
if (-not (Test-Path (Join-Path $BuiltData "items.txt"))) { throw "Run tools\extract_mc.py first ($BuiltData)" }

$added = New-Object System.Collections.Generic.List[string]
if (Test-Path $Manifest) { $added.AddRange([string[]](Get-Content $Manifest)) }
function Track($rel) { if (-not $added.Contains($rel)) { $added.Add($rel) } }

# ScriptHookV + Legacy ASI loader (dinput8.dll). Never the zip's xinput1_4.dll: that's the Enhanced loader.
foreach ($f in "ScriptHookV.dll", "dinput8.dll") {
	$dst = Join-Path $GameDir $f
	if (Test-Path $dst) {
		if ($added.Contains($f)) { Copy-Item (Join-Path $Runtime $f) $dst -Force }
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
Get-ChildItem $BuiltData -Recurse -File | ForEach-Object {
	$rel = $_.FullName.Substring($BuiltData.Length + 1)
	if ($rel -in "world.txt", "config.ini") { return }
	$dst = Join-Path $Data $rel
	New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
	Copy-Item $_.FullName $dst -Force
}
Write-Host "installed GrandTheftMinecraft\ data folder"
$added | Set-Content -Path $Manifest -Encoding UTF8
Write-Host "Done. Launch GTA from this folder (PlayGTAV.exe), Story Mode only. F6 toggles Minecraft mode."
