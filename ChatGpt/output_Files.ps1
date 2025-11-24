<#
.SYNOPSIS
    Reads content from files in given directories and/or individual files,
    matching specified extensions (for directories),
    and excluding folders whose names contain any of the specified patterns.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ValueFromPipeline, ValueFromPipelineByPropertyName)]
    [Alias('Path')]
    [ValidateScript({ 
        if (-not (Test-Path -Path $_)) {
            throw "Path '$_' does not exist."
        }
        $true
    })]
    [string[]]$Directory,   # Peut contenir des fichiers OU dossiers

    [Parameter(Mandatory = $true)]
    [string[]]$Extensions,

    [Parameter(Mandatory = $false)]
    [string[]]$ExcludeFolderPattern = @()
)

function Write-FileDetails {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$FileInfo
    )

    Write-Output $FileInfo.Name
    Write-Output "-----------------------------------------"
    Write-Output $FileInfo.FullName
    Write-Output "-----------------------------------------"

    try {
        $content = Get-Content -Path $FileInfo.FullName -Raw -Encoding UTF8 -ErrorAction Stop
        Write-Output $content
    }
    catch {
        Write-Output "Error reading file content: $($_.Exception.Message)"
    }

    Write-Output "========================"
}

# --- Parcours des entrées ---
$allFiles = @()

foreach ($path in $Directory) {
    if (Test-Path $path -PathType Container) {
        # C’est un dossier → on liste les fichiers selon extensions
        $filesInDir = Get-ChildItem -Path $path -Recurse -File |
            Where-Object {
                $exclude = $false
                foreach ($pattern in $ExcludeFolderPattern) {
                    if ($_.DirectoryName.Contains($pattern)) {
                        $exclude = $true
                        break
                    }
                }
                -not $exclude
            } |
            Where-Object { $Extensions -contains $_.Extension }

        $allFiles += $filesInDir
    }
    elseif (Test-Path $path -PathType Leaf) {
        # C’est un fichier individuel
        $allFiles += Get-Item $path
    }
}

# --- Lecture et affichage des fichiers ---
foreach ($file in $allFiles) {
    Write-FileDetails -FileInfo $file
}