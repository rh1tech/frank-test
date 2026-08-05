/*
 * FRANK universal test firmware
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-test
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * diskio.c — FatFs on the SD socket.
 *
 * One volume, and it is the card. The layer underneath is
 * drivers/sdblock, which owns the pins while a report is being written
 * and gives them back afterwards; FatFs never sees any of that.
 *
 * There is no real-time clock behind get_fattime(). The DS3231 would
 * serve, but only four boards have one and a timestamp that is real on
 * some boards and invented on others is worse than one that is plainly a
 * placeholder everywhere. The report writes its own timestamp into the
 * text, where it can say where it came from.
 */

#include "ff.h"       /* the types diskio.h uses come from here */
#include "diskio.h"

#include "sdblock.h"

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return sdblock_sectors() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    /* The card is brought up by whoever asked for the report, because
     * that is who knows which board's pins to use. */
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    return sdblock_read((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    return sdblock_write((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) return RES_PARERR;

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;                       /* writes block already */
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = sdblock_sectors();
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;                  /* erase block, unknown */
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

/* FatFs stamps files with this. Nothing here knows the date, and the
 * report carries its own timestamp in the text, so this is a fixed and
 * obviously artificial value rather than a plausible lie. */
DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}
