# Build the M25-4b flash loader (TriCore, HighTec toolchain)
$ErrorActionPreference = 'Stop'
$tc = 'C:\Infineon\AURIX-Configuration-Studio-1.0.24\eclipse\tricore-gcc11\bin'
Push-Location $PSScriptRoot
try {
    & "$tc\tricore-elf-gcc.exe" -c -O2 -nostdlib -ffreestanding -o m25_flash_loader.o m25_flash_loader.c
    & "$tc\tricore-elf-gcc.exe" -nostdlib -T m25_flash_loader.ld -o m25_flash_loader.elf m25_flash_loader.o
    & "$tc\tricore-elf-objcopy.exe" -O binary m25_flash_loader.elf m25_flash_loader.bin
    Get-Item m25_flash_loader.bin | Select-Object Name, Length, LastWriteTime
}
finally {
    Pop-Location
}
