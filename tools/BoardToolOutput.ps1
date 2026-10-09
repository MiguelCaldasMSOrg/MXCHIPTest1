function Write-BoardVerified {
  param([Parameter(Mandatory)][string]$Message)
  Write-Host "Verified: $Message" -ForegroundColor Green
}
