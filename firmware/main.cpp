// CommandLine.cpp : Defines the entry point for the
//                   console application.
//

#define MAX_PROBES      2
#define W1_PIN      PB_9

#include "mbed.h"
#include "DS1820.h"
#include "SDFileSystem.h"
#include "millis.h"
#include "Watchdog.h"
#include <string>
#include <vector>

extern "C" {
#include "CommandProcessor.h"
}

Watchdog wdt;

Serial btserial(PB_10, PB_11); // serial communication (HC-05 in this case)

SDFileSystem sd(PA_7, PA_6, PA_5, PA_4, "sd");  //mosi, miso, sck, cs

AnalogIn   pressin(PA_1);       // pressure transducer adc pin
DigitalOut ledout(PC_13);       // builtin led
DS1820* probe[MAX_PROBES];      // software onewire bus

Ticker measureTick;             // measurement ticker

vector<string> filenames; //filenames are stored in a vector string
bool dsstarted = false;
float   temp = 0;
uint8_t dserror = 0;
uint8_t mode = 0;
char filename[32];
char longfilename[48];
char buffer [128];


void listdir(void) // FIX THIS
{
    DIR *d;
    struct dirent *p;

    d = opendir("/sd");
    btserial.printf("d: %i", d);
    if (d != NULL) {
        btserial.printf("p: %i", p);
        while ((p = readdir(d)) != NULL) {
            printf(" - %s\r\n", p->d_name);
        }
    } else {
        btserial.printf("\r\nCould not open directory!\r\n");
    }
    closedir(d);
}

bool sendfile(char* fname, bool test)
{
    bool stat = false;
    FILE *fp = fopen(fname, "r");
    if(fp == NULL) {
        btserial.printf("Could not open file for read\r\n");
        stat = 1;
    } else {
        while(fgets (buffer, 32, fp) != NULL) {
            btserial.printf("%s",buffer); // and show it in char format
        }
        if (test) {
            fseek(fp, 0, SEEK_END); // seek to end of file
            int size = ftell(fp);       // get current file pointer
            fseek(fp, 0, SEEK_SET); // seek back to beginning of file
            btserial.printf("File size: %i bytes\r\n",size);
        }
        stat = 0;
    }
    fclose(fp);
    return stat;
}

void sdtst(void)
{
    startMillis();
    btserial.printf("Trying to test writing...\r\n");
    bool writetest = 1;
    bool readtest = 1;
    FILE *fp = fopen("/sd/sdcheck.txt", "w");
    if(fp == NULL) {
        btserial.printf("Could not open file for write\r\n");
        writetest = 1;
        fclose(fp);
    } else {
        for (int i = 0; i < 5; i++) {
            fprintf(fp, "%i:%d\r\n", i, millis());
        }
        writetest = 0;
        fclose(fp);
        btserial.printf("Trying to self-test reading...\r\n");
        readtest = sendfile("/sd/sdcheck.txt", 1);
    }
    if (!(writetest || readtest))
        btserial.printf("\r\nSD check OK\r\n");
    else
        btserial.printf("\r\nSD check FAILED!\r\n");
    stopMillis();
}

void onMeasureTick(void)       // function to call every tick
{
    bool err = 0;
    ledout = !ledout;                 //  toggle the LED
    if (!sd.disk_initialize()) { // disk initialized with code 0
        sd.mount();
        FILE *fp = fopen(longfilename, "a");
        if(fp == NULL) {
            btserial.printf("Could not open file '%s' for write\r\n", filename);
            err = 1;
        } else {
            if (dsstarted) {
                probe[0]->convertTemperature(true, DS1820::all_devices);         //Start temperature conversion, wait until ready
                //wait(.1);
                float pressure = pressin.read();
                //wait(.1);
                temp = probe[0]->temperature();
                if ((abs(temp) > 0.001) && (abs(pressure > 0.001)) && (abs(pressure < 100))) {
                    btserial.printf("Millis:%d | T:%.3f | P:%.3f\r\n",millis(), temp, pressure);
                    fprintf(fp, "%d;%.3f;%.3f\r\n",millis(),temp,pressure);
                }
                err = 0;
            } else {
                btserial.printf("Problem with DS18B20 init\r\n", filename);
                err = 1;
            }
        }
        fclose(fp);
    } else
        btserial.printf("Problem with SD card\r\n"); // ooops, not initialized, code 1
    sd.unmount();
    if (err) btserial.printf("Measuring mode run error\r\n");
}


RUNRESULT_T Check(char *p);
const CMD_T CheckCmd = {
    "Check",
    "Check control of subsystems",
    Check,
    visible
};

RUNRESULT_T Mode(char *p);
const CMD_T ModeCmd = {
    "Mode",
    "Select run mode (0 - do nothing; 1 - logging mode)",
    Mode,
    visible
};

RUNRESULT_T Ls(char *p);
const CMD_T LsCmd = {
    "Ls",
    "List of files in %arg% directory",
    Ls,
    visible
};

RUNRESULT_T Filename(char *p);
const CMD_T FilenameCmd = {
    "Filename",
    "Get filename (w/o args) or send new filename to gauge",
    Filename,
    visible
};

RUNRESULT_T FileGet(char *p);
const CMD_T FileGetCmd = {
    "Get",
    "Get file %filename%",
    FileGet,
    visible
};

RUNRESULT_T SignOnBanner(char *p);
const CMD_T SignOnBannerCmd = {
    "About",
    "Banner on start",
    SignOnBanner,
    invisible
};

RUNRESULT_T SignOnBanner(char *p)
{
    ledout = 0;
    btserial.puts("SAGITTA PROJECT BETA MEETING YOU\r\n");//This program was built " __DATE__ " " __TIME__ ".\r\n");
    return runok;
}

RUNRESULT_T Ls(char *p)
{
    ledout = 0;
    listdir();
    for(vector<string>::iterator it=filenames.begin(); it < filenames.end(); it++) {
        printf("%s\n\r",(*it).c_str());
    }
    return runok;
}

RUNRESULT_T Filename(char *p)
{
    ledout = 0;
    if (*p) {
        strcpy (filename, p);
        sprintf(longfilename, "/sd/%s", filename);
    } else
        btserial.printf("\r%s\r\n", filename);
    btserial.puts("\r\nsuccess\r\n");
    return runok;
}

RUNRESULT_T FileGet(char *p)
{
    if (!(*p))
        p = filename;
    ledout = 0;
    if (!sd.disk_initialize()) { // disk initialized with code 0
        sd.mount();
        btserial.puts("\r\n_start_file\r\n");
        char fname[48];
        sprintf(fname, "/sd/%s", p);
        sendfile(fname, 0);
        btserial.puts("\r\n_end_file\r\n");
    } else
        btserial.printf("Problem with SD card\r\n"); // ooops, not initialized, code 1
    sd.unmount();
    return runok;
}


RUNRESULT_T Mode(char *p)
{
    ledout = 0;
    if (strrchr(p, '0')) { //stop mode activated
        btserial.printf("\r\ndeactivated\r\n");
        mode = 0;
        measureTick.detach();
        stopMillis();
    } else if (strrchr(p, '1')) { //run mode activated
        btserial.printf("\r\nactivated\r\n");
        mode = 1;
        startMillis();
        measureTick.attach(&onMeasureTick,0.33);  // attach the onTick function to the ticker at a period of 0.33 seconds
    } else
        btserial.printf("\r\nbad mode\r\n");
    return runok;
}


RUNRESULT_T Check(char *p)
{
    if (mode == 0) {
        ledout = 0;
        btserial.printf("Pressure sensor: %.3f\r\n", pressin.read());

        if (dsstarted) {
            wait(.33);
            btserial.printf("Millis before ready:%d\r\n",millis());
            probe[0]->convertTemperature(true, DS1820::all_devices);  //Start temperature conversion, wait until ready (maybe problem but we will use NTC sensor)
            btserial.printf("Millis after ready:%d\r\n",millis());
            temp = probe[0]->temperature();
            btserial.printf("Temp sensor = %3.3f\r\n", temp);

        } else
            btserial.printf ("\r\nTemp sensor not present\r\n");

        if (!sd.disk_initialize()) { // disk initialized with code 0
            sd.mount();
            sdtst();
        } else
            btserial.printf("Problem with SD card\r\n"); // ooops, not initialized, code 1
        sd.unmount();
    }
    return runok;
}

// Provide the serial interface methods for the command processor
int mReadable()
{
    return btserial.readable();
}
int mGetCh()
{
    return btserial.getc();
}
int mPutCh(int a)
{
    return btserial.putc(a);
}
int mPutS(const char * s)
{
    return btserial.printf("%s\r\n", s);
}

int main(int argc, char* argv[])
{
    strcpy (filename, "default.csv");
    sprintf(longfilename, "/sd/%s", filename);

    CMDP_T * cp = GetCommandProcessor();

    wdt.Configure (10.0);

    //btserial.baud(115200);
    btserial.baud(9600);
    cp->Init(
        &SignOnBannerCmd,
        0 | CFG_ENABLE_SYSTEM
        | 0 | CFG_CASE_INSENSITIVE,   // options
        80,         // Command Buffer length
        5,          // History depth # of commands
        mReadable,  // User provided API (kbhit())
        mGetCh,     // User provided API
        mPutCh,     // User provided API
        mPutS);     // User provided API

    // Start adding custom commands now
    cp->Add(&FilenameCmd);
    cp->Add(&FileGetCmd);
    cp->Add(&CheckCmd);
    cp->Add(&LsCmd);
    cp->Add(&ModeCmd);

    // Should never "wait" in here

    if (wdt.WatchdogCausedReset())
        btserial.printf("ERROR: Gauge has been restarted by watchdog\n");

    // Initialize the probe array to DS1820 objects
    int num_devices = 0;

    while(DS1820::unassignedProbe(W1_PIN)) {
        probe[num_devices] = new DS1820(W1_PIN);
        num_devices++;
        if (num_devices == MAX_PROBES)
            break;
    }
    dsstarted = num_devices;

    while (cp->Run() == runok) { // run this timeslice
        // do non-blocking things here
        wdt.Service();       // kick the dog before the timeout
        ledout = 1;
    }
    cp->End();  // cleanup
    return 0;
}