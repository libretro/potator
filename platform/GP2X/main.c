#include <dirent.h> 
#include <sys/types.h> 
#include <sys/param.h> 
#include <sys/stat.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>
#include <stdarg.h>

#include "supervision.h"

#include "minimal.h"
#include "menues.h"

BOOL paused = FALSE;

uint8 *buffer;
unsigned int bufferSize = 0;

volatile unsigned char svFrm = 0;
volatile unsigned char xFrm = 0;
volatile unsigned char FPS = 0;

unsigned short *screen16;
unsigned short screenbuffer[161 * 161];

const char *romname;

currentConfig_t currentConfig;

uint16 mapRGB(uint8 r, uint8 g, uint8 b)
{
    return gp2x_video_RGB_color16(r, g, b);
}

int LoadROM(const char *filename)
{
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }

    romname = filename;

    FILE *romfile = fopen(filename, "rb");
    if (romfile == NULL) {
        printf("fopen(): Unable to open file!\n");
        return 1;
    }
    fseek(romfile, 0, SEEK_END);
    bufferSize = ftell(romfile);
    fseek(romfile, 0, SEEK_SET);

    buffer = (uint8 *)malloc(bufferSize);

    fread(buffer, bufferSize, 1, romfile);

    if (fclose(romfile) == EOF) {
        printf("fclose(): Unable to close file!\n");
        return 1;
    }
    return 0;
}

void CheckKeys(void)
{
    unsigned long pad = gp2x_joystick_read(0);

    uint8 controls_state = 0;
    if (pad & GP2X_UP)     controls_state |= 0x08;
    if (pad & GP2X_RIGHT)  controls_state |= 0x01;
    if (pad & GP2X_LEFT)   controls_state |= 0x02;
    if (pad & GP2X_DOWN)   controls_state |= 0x04;
    if (pad & GP2X_UP)     controls_state |= 0x08;
    if (pad & GP2X_X)      controls_state |= 0x10;
    if (pad & GP2X_A)      controls_state |= 0x20;
    if (pad & GP2X_START)  controls_state |= 0x80;
    if (pad & GP2X_SELECT) controls_state |= 0x40;

    supervision_set_input(controls_state);

    if ((pad & GP2X_VOL_DOWN) && (pad & GP2X_START)) {
        supervision_done();
        //gp2x_deinit();
        exit(0);
    }

    if ((pad & GP2X_L) && (pad & GP2X_R)) {
        supervision_reset();
        supervision_set_map_func(mapRGB);
    }

    if ((pad & GP2X_L) && (pad & GP2X_LEFT))
        supervision_set_color_scheme(SV_COLOR_SCHEME_DEFAULT);

    if ((pad & GP2X_L) && (pad & GP2X_RIGHT))
        supervision_set_color_scheme(SV_COLOR_SCHEME_AMBER);

    if ((pad & GP2X_L) && (pad & GP2X_UP))
        supervision_set_color_scheme(SV_COLOR_SCHEME_GREEN);

    if ((pad & GP2X_L) && (pad & GP2X_DOWN))
        supervision_set_color_scheme(SV_COLOR_SCHEME_BLUE);

    if (pad & GP2X_Y) {
        paused = TRUE;
        textClear();
        handleMainMenu();
        paused = FALSE;
    }

    if (pad & (GP2X_VOL_UP | GP2X_VOL_DOWN)) {
        int vol = currentConfig.volume;
        if (pad & GP2X_VOL_UP) {
            if (vol < 255) vol++;
        } else {
            if (vol >   0) vol--;
        }
        gp2x_sound_volume(vol, vol);
        currentConfig.volume = vol;
    }
}

int main(int argc, char *argv[])
{
    gp2x_init(1000, 16, 11025, 16, 1, 60, 1);

    screen16 = (unsigned short *)gp2x_video_RGB[0].screen;

    //char temp[255];

    if (argc <= 1) {
        printf("\nNot enough arguments.\n");
    } else {
        romname = strdup(argv[1]);
        FILE *in = NULL;
        in = fopen(romname, "r");
        if (in == NULL) {
            printf("The file %s doesn't exist.\n", romname);
        }
        fclose(in);
    }

    supervision_init();

    getRunDir();

    if (romname != NULL){
        LoadROM(romname);
        supervision_load(buffer, (uint32)bufferSize);
        supervision_set_map_func(mapRGB);
    } else {
        handleFileMenu();
    }

    emu_ReadConfig();

    gp2x_sound_volume(255, 255);
    gp2x_sound_pause(0);

    while(1)
    {
        CheckKeys();

        while(!paused)
        {
            CheckKeys(); //key control

            switch(currentConfig.videoMode){
            case 0: {
                int j;
                supervision_exec(screenbuffer, FALSE);
                for (j = 0; j < 160; j++)
                    gp2x_memcpy(screen16+(80+(j+40)*320), screenbuffer+(j * 160), 160*2);
            }
                break;
            case 1:
            case 2:
                supervision_exec(screen16, FALSE);
                break;
            default:
                break;
            }

            /*gp2x_video_waitvsync();

            sprintf(temp,"FPS: %3d", FPS);
            gp2x_printf(NULL,0,0,temp);
            ++svFrm;*/

            gp2x_video_RGB_flip(0);
        }
    }
    supervision_done();
    gp2x_deinit();
    return 0;
}

void gp2x_sound_frame(void *blah, void *buffer, int samples)
{
    short *buffer16 = (short*)buffer;

    while (samples--) {
        if(currentConfig.enable_sound){
            // TODO:
        } else {
            *buffer16++ = 0; //Left
            *buffer16++ = 0; //Right
        }
    }
}
