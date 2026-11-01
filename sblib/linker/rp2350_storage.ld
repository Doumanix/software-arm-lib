/*
 * rp2350_storage.ld - Persistent storage of the sblib at the end of the flash (RP2350).
 *
 * Added to the Pico SDK linker script by the CMake target sblib_rp2350 (-T). The size is
 * passed with --defsym=__sblib_storage_size=<bytes> (CMake variable SBLIB_RP2350_STORAGE_SIZE).
 *
 * The storage holds the data written with the IAP functions (UserEeprom in the last sector,
 * MemMapper). iapProgram() and iapErase...() only write inside this region, so the firmware
 * can never be overwritten. The link fails if the firmware reaches into the region.
 *
 * Only symbols and assertions, no SECTIONS: the script may come before the Pico SDK linker
 * script on the command line. The expressions are evaluated after all scripts are read.
 */

__sblib_storage_end = ORIGIN(FLASH) + LENGTH(FLASH);
__sblib_storage_start = __sblib_storage_end - __sblib_storage_size;

ASSERT(__sblib_storage_size % 4096 == 0, "sblib: SBLIB_RP2350_STORAGE_SIZE must be a multiple of the 4 KiB flash sector")
ASSERT(__flash_binary_end <= __sblib_storage_start, "sblib: firmware overlaps the persistent storage at the end of the flash")
