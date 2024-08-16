#include <kernel.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <libcdvd.h>
#include <debug.h>
#include <malloc.h>
#include <string.h>
#include <osd_config.h>
#include <gsKit.h>
#include <stdint.h>
#include <ctype.h>  // For toupper used to convert to uppercase
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>

// elf loading
#include <iopcontrol.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <elf-loader.h>
#include <dirent.h>

#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <fileXio_rpc.h>
#include <io_common.h>

#include "cnf_lite.h"

extern unsigned char ps2dev9_irx[];
extern unsigned int size_ps2dev9_irx;

extern unsigned char ps2atad_irx[];
extern unsigned int size_ps2atad_irx;

extern unsigned char ps2hdd_irx[];
extern unsigned int size_ps2hdd_irx;

extern unsigned char iomanx_irx[];
extern unsigned int size_iomanx_irx;

extern unsigned char filexio_irx[];
extern unsigned int size_filexio_irx;

extern unsigned char ps2fs_irx[];
extern unsigned int size_ps2fs_irx;

extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;

extern unsigned char usbmass_bd_irx[];
extern unsigned int size_usbmass_bd_irx;

enum CONSOLE_REGIONS {
    CONSOLE_REGION_INVALID = -1,
    CONSOLE_REGION_JAPAN   = 0,
    CONSOLE_REGION_USA, // USA and HK/SG.
    CONSOLE_REGION_EUROPE,
    CONSOLE_REGION_CHINA,

    CONSOLE_REGION_COUNT
};

enum DISC_REGIONS {
    DISC_REGION_INVALID = -1,
    DISC_REGION_JAPAN   = 0,
    DISC_REGION_USA,
    DISC_REGION_EUROPE,

    DISC_REGION_COUNT
};

static char ver[64], cdboot_path[256], romver[16];

static char ConsoleRegion = CONSOLE_REGION_INVALID, DiscRegion = DISC_REGION_INVALID;

// Define a constant for the maximum length of the game ID
#define MAX_GAME_ID_LENGTH 12
char game_id[MAX_GAME_ID_LENGTH];

char cwd[256];
const char *targetElf = "PS1VModeNeg.elf";
char fullFilePath[512];

static int GetDiscRegion(const char *path) {
    int region = DISC_REGION_INVALID;

    // Find the last occurrence of '/', '\\', or ':'
    const char *path_end = strrchr(path, '/');
    if (!path_end) path_end = strrchr(path, '\\');
    if (!path_end) path_end = strrchr(path, ':');

    // If we found a separator, move past it to get the filename
    if (path_end) {
        path_end++;  // Move past the separator

        // Copy the filename to `game_id` and adjust length
        strncpy(game_id, path_end, MAX_GAME_ID_LENGTH - 1);
        game_id[MAX_GAME_ID_LENGTH - 1] = '\0'; // Ensure null-termination

        // Convert `game_id` to uppercase
        for (char *p = game_id; *p; ++p) {
            *p = toupper((unsigned char)*p);
        }

        // Determine the region based on the third character of the filename
        if (strlen(game_id) >= 3) {
            switch (game_id[2]) {
                case 'P':
                    region = DISC_REGION_JAPAN;
                    break;
                case 'U':
                    region = DISC_REGION_USA;
                    break;
                case 'E':
                    region = DISC_REGION_EUROPE;
                    break;
            }
        }
    }

    return region;
}

static unsigned short int GetBootROMVersion(void)
{
    int fd;
    char VerStr[5];

    fd = open("rom0:ROMVER", O_RDONLY);
    read(fd, romver, sizeof(romver));
    close(fd);
    VerStr[0] = romver[0];
    VerStr[1] = romver[1];
    VerStr[2] = romver[2];
    VerStr[3] = romver[3];
    VerStr[4] = '\0';

    return ((unsigned short int)strtoul(VerStr, NULL, 16));
}

static int GetConsoleRegion(void)
{
    int result;

    if ((result = ConsoleRegion) < 0) {
        switch (romver[4]) {
            case 'C':
                ConsoleRegion = CONSOLE_REGION_CHINA;
                break;
            case 'E':
                ConsoleRegion = CONSOLE_REGION_EUROPE;
                break;
            case 'H':
            case 'A':
                ConsoleRegion = CONSOLE_REGION_USA;
                break;
            case 'J':
                ConsoleRegion = CONSOLE_REGION_JAPAN;
        }

        result = ConsoleRegion;
    }

    return result;
}

static int HasValidDiscInserted(int mode) {
    int DiscType, result;

    if (sceCdDiskReady(mode) == SCECdComplete) {
        switch ((DiscType = sceCdGetDiskType())) {
            case SCECdDETCT:
            case SCECdDETCTCD:
            case SCECdDETCTDVDS:
            case SCECdDETCTDVDD:
                result = 0;
                break;
            case SCECdUNKNOWN:
            case SCECdPSCD:
            case SCECdPSCDDA:
            case SCECdPS2CD:
            case SCECdPS2CDDA:
            case SCECdPS2DVD:
            case SCECdCDDA:
            case SCECdDVDV:
            case SCECdIllegalMedia:
                result = DiscType;
                break;
            default:
                result = -1;
        }
    } else {
        result = -1;
    }

    return result;
}

static void DelayIO(void) {
    int i;

    for (i = 0; i < 24; i++)
        nopdelay();
}

// Game ID function to calculate CRC
static uint8_t gameid_calc_crc(const uint8_t *data, int len) {
    uint8_t crc = 0x00;
    for (int i = 0; i < len; i++) {
        crc += data[i];
    }
    return 0x100 - crc;
}

// Function to draw game ID using GSKit
void DrawGameID(GSGLOBAL *gsGlobal, int screenWidth, int screenHeight, const char* game_id) {
    uint8_t data[64] = { 0 };
    int gidlen = strnlen(game_id, 11);  // Ensure the length does not exceed 11 characters

    int dpos = 0;
    data[dpos++] = 0xA5;  // detect word
    data[dpos++] = 0x00;  // address offset
    dpos++;
    data[dpos++] = gidlen;

    memcpy(&data[dpos], game_id, gidlen);
    dpos += gidlen;

    data[dpos++] = 0x00;
    data[dpos++] = 0xD5;  // end word
    data[dpos++] = 0x00;  // padding

    int data_len = dpos;
    data[2] = gameid_calc_crc(&data[3], data_len - 3);

    int xstart = (screenWidth / 2) - (data_len * 8);
    int ystart = screenHeight - (((screenHeight / 8) * 2) + 20);
    int height = 2;

    for (int i = 0; i < data_len; i++) {
        for (int ii = 7; ii >= 0; ii--) {
            int x = xstart + (i * 16 + ((7 - ii) * 2));
            int x1 = x + 1;

            gsKit_prim_sprite(gsGlobal, x, ystart, x1, ystart + height, 1, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x80));

            uint32_t color = (data[i] >> ii) & 1 ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x80);
            gsKit_prim_sprite(gsGlobal, x1, ystart, x1 + 1, ystart + height, 1, color);
        }
    }
}

void DisplayGameID() {
    GSGLOBAL *gsGlobal = gsKit_init_global();

    // Set the screen mode (e.g., GS_MODE_PAL, GS_MODE_NTSC)
    gsGlobal->Mode = GS_MODE_DTV_480P;  // Set to 480p mode
    gsGlobal->Width = 640;  // Width of the screen
    gsGlobal->Height = 448;  // Height of the screen
    gsGlobal->Interlace = GS_NONINTERLACED;  // Non-interlaced mode for progressive scan
    gsGlobal->Field = GS_FRAME;  // Frame mode
    gsGlobal->PSM = GS_PSM_CT24;  // Pixel storage format (24-bit color)
    gsGlobal->PSMZ = GS_PSMZ_32;  // Z buffer format (32-bit depth)
    gsGlobal->DoubleBuffering = GS_SETTING_OFF;  // Disable double buffering
    gsGlobal->ZBuffering = GS_SETTING_OFF;  // Disable Z buffering

    // Initialize the screen
    gsKit_vram_clear(gsGlobal);  // Clear VRAM
    gsKit_init_screen(gsGlobal);

    // Display game ID
    DrawGameID(gsGlobal, gsGlobal->Width, gsGlobal->Height, game_id);

    // Sync and flip the GS (Graphics Synthesizer)
    gsKit_sync_flip(gsGlobal);
    gsKit_queue_exec(gsGlobal);
}

static void LoadModules()
{
    SifInitIopHeap();
    SifLoadFileInit();
    sbv_patch_enable_lmb();
    SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, NULL);
    SifExecModuleBuffer(iomanx_irx, size_iomanx_irx, 0, NULL, NULL);
    SifExecModuleBuffer(filexio_irx, size_filexio_irx, 0, NULL, NULL);
    fileXioInit();
    SifExecModuleBuffer(ps2atad_irx, size_ps2atad_irx, 0, NULL, NULL);
    SifExecModuleBuffer(ps2hdd_irx, size_ps2hdd_irx, 0, NULL, NULL);
    SifExecModuleBuffer(ps2fs_irx, size_ps2fs_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&usbd_irx, size_usbd_irx, 0, NULL, NULL);
    SifExecModuleBuffer(&usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, NULL);
    SifLoadFileExit();
    SifExitIopHeap();      
}

// Function to check if PS1VModeNeg.elf exists in the current directory
static int FindElfFile() {
    DIR *d;
    struct dirent *dir;
    d = opendir(".");  // Open the current directory
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcasecmp(dir->d_name, targetElf) == 0) {
                closedir(d);
                return 0; // Success: File found
            }
        }
        closedir(d);
    }

    // Next, check the pfs0:/ directory
    d = opendir("pfs0:/");  // Open the pfs0:/ directory
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcasecmp(dir->d_name, targetElf) == 0) {
                closedir(d);
                return 0; // Success: File found in the pfs0:/ directory
            }
        }
        closedir(d);
    }

    return -1; // Failure: File not found
}

int main(int argc, char *argv[]) {
    int DiscType, done;
    SifInitRpc(0);
    fioInit();
    sceCdInit(SCECdINoD);
    init_scr();

    done = 0;
    do {
        if ((DiscType = HasValidDiscInserted(1)) >= 0) {
            if (DiscType == 0) {
                while (HasValidDiscInserted(1) == 0) {
                    DelayIO();
                }
            } else {
                switch (DiscType) {
                    case SCECdPSCD:
                    case SCECdPSCDDA:
                    case SCECdPS2CD:
                    case SCECdPS2CDDA:
                    case SCECdPS2DVD:
                        done = 1;
                        break;
                    default:
                        scr_clear();
                        scr_printf("Retro Gem Disc Launcher\n"
                                   "=======================\n\n");
                        scr_printf("Unsupported disc. Please insert a PlayStation or PlayStation 2 game disc.");
                        sceCdStop();
                        sceCdSync(0);
                        while (HasValidDiscInserted(1) > 0) {
                            DelayIO();
                        }
                }
            }
        } else {
            scr_clear();
            scr_printf("Retro Gem Disc Launcher\n"
                       "=======================\n\n");
            scr_printf("Please insert a PlayStation or PlayStation 2 game disc.");
            while (HasValidDiscInserted(1) < 0) {
                DelayIO();
            }
        }
    } while (!done);
    scr_clear();
    sceCdDiskReady(0);
    Read_SYSTEM_CNF(cdboot_path, ver);
    DiscType = sceCdGetDiskType();
    DiscRegion = GetDiscRegion(cdboot_path);
    GetBootROMVersion();
    GetConsoleRegion();

    if ((DiscType == SCECdPSCD) || (DiscType == SCECdPSCDDA)) { // If PS1 game disc
        char *args[2] = {cdboot_path, ver};

        // If PS1 game has a different video mode to the console
        if (((DiscRegion == 2) && (ConsoleRegion != 2)) ||
            ((DiscRegion == 0 || DiscRegion == 1) && (ConsoleRegion == 2))) {

            // Initialize and configure environment
            if (argc > 1) {
                while (!SifIopReset(NULL, 0)) {}
                while (!SifIopSync()) {}
                SifInitRpc(0);
            }

            LoadModules();

            // Get the current working directory
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                DisplayGameID();
                LoadExecPS2("rom0:PS1DRV", 2, args);
                return 1;
            }

            // Check if the current working directory is on the HDD (pfs)
            if (strstr(cwd, "pfs") != NULL) {
                char *pfs_pos = strstr(cwd, ":pfs:");
                if (pfs_pos) *pfs_pos = '\0'; // Terminate string before ":pfs:"

                if (fileXioMount("pfs0:", cwd, FIO_MT_RDONLY) != 0) {
                    DisplayGameID();
                    LoadExecPS2("rom0:PS1DRV", 2, args);
                    return 1;
                }
                strcpy(cwd, "pfs0:/");
            }

            // Find and load the ELF file
            if (FindElfFile() == 0) {
                // Ensure that cwd ends with a '/'
                size_t cwd_len = strlen(cwd);
                if (cwd[cwd_len - 1] != '/') {
                // Append '/' if it's not already there
                strncat(cwd, "/", sizeof(cwd) - cwd_len - 1);
                }

                // Construct the full file path
                snprintf(fullFilePath, sizeof(fullFilePath), "%s%s", cwd, targetElf);
                DisplayGameID();
                LoadELFFromFile(fullFilePath, 0, NULL);
                return 0;
            } else {
                DisplayGameID();
                LoadExecPS2("rom0:PS1DRV", 2, args);
                return 1;
        }

        } else {
            // Run the normal PS1DRV command
            DisplayGameID();
            LoadExecPS2("rom0:PS1DRV", 2, args);
            return 0;
        }
    } else {
        // If PS2 game disc
        DisplayGameID();
        LoadExecPS2(cdboot_path, 0, NULL);
        return 0;
    }

    fioExit();
    sceCdInit(SCECdEXIT);
    SifExitRpc();

    return 0;
}