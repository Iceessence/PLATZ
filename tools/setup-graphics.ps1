[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
& py -3 "$PSScriptRoot/setup_graphics.py"
if ($LASTEXITCODE) { throw 'Graphics development dependencies could not be prepared.' }
