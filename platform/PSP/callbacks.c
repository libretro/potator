#include <pspkernel.h>

static int exitRequest = 0;

int running()
{
	return !exitRequest;
}

static int exitCallback(int arg1, int arg2, void *common)
{
	exitRequest = 1;
	return 0;
}

static int callbackThread(SceSize args, void *argp)
{
	int cbid;

	cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
	sceKernelRegisterExitCallback(cbid);

	sceKernelSleepThreadCB();

	return 0;
}

int setupCallbacks(void)
{
	int thid = 0;

	thid = sceKernelCreateThread("update_thread", callbackThread, 0x11, 0xFA0, 0, NULL);
	if(thid >= 0) {
		sceKernelStartThread(thid, 0, 0);
	}

	return thid;
}
