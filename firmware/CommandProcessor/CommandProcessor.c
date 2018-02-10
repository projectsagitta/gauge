/// @file CommandProcessor.c is a simple interface to an interactive
///         command set of user defined commands.
///
/// With this, you can create functions that are exposed to a console
/// interface. Each command may have 0 or more parameters.
/// Typing the command, or at least the set of characters that make
/// it unique from all other commands is enough to activate the command.
///
/// Even though it is a c interface, it is somewhat object oriented.
///
/// @note Copyright &copr; 2011 by Smartware Computing, all rights reserved.
///     Individuals may use this application for evaluation or non-commercial
///     purposes. Within this restriction, changes may be made to this application
///     as long as this copyright notice is retained. The user shall make
///     clear that their work is a derived work, and not the original.
///     Users of this application and sources accept this application "as is" and
///     shall hold harmless Smartware Computing, for any undesired results while
///     using this application - whether real or imagined.
///
/// @author David Smart, Smartware Computing
///

#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#ifdef WIN32
#include "windows.h"
#pragma warning (disable: 4996)
#endif

#include "CommandProcessor.h"

/// This holds the single linked list of commands
/// @verbatim
/// +-- Head->next
/// v
/// +-- p       +-- n
/// v           v
/// |menu|-------------------------------->|"Command"  |
/// |next|->0   |menu|---->|"Help"         |"Help"     |
///             |next|->0  |"..."          |*(callback)|
///                        |*(callback)    |visible    |
///                        |visible
/// @endverbatim
///
typedef struct CMDLINK_T {
    CMD_T * menu;                // handle to the menu item
    struct CMDLINK_T * next;    // handle to the next link
} CMDLINK_T;

static CMDLINK_T * head = NULL;

static char *buffer;        // buffer space must be allocated based on the longest command
static char *historyBuffer;        // keeps the history of commands for recall
static int historyCount = 0;    // and the count of
static int historyDepth = 0;
static size_t longestCommand = 0;

static struct {
    CMD_T *SignOnBanner;
    int showSignOnBanner;        // Shows the sign-on banner at startup
    int caseinsensitive;    // FALSE=casesensitive, TRUE=insensitive
    int echo;               // TRUE=echo on, FALSE=echo off
    int bufferSize;         // size of the command buffer
    int (*kbhit)(void);
    int (*getch)(void);
    int (*putch)(int ch);
    int (*puts)(const char * s);
} cfg;

static INITRESULT_T CommandProcessor_Init(
    CMD_T *SignOnBanner,
    CONFIG_T config,
    int maxCmdLen,
    int historyCount,
    int (*kbhit)(void),
    int (*getch)(void),
    int (*putch)(int ch),
    int (*puts)(const char * s)
);

// Used when processing characters
static int keycount = 0;    // how full?
static int leadinChar = 0;
static int whereInHistory = 0;        // navigates history
static int showPrompt = TRUE;


static ADDRESULT_T CommandProcessor_Add(CMD_T *m);
static RUNRESULT_T CommandProcessor_Run(void);
static RUNRESULT_T CommandProcessor_End(void);
static RUNRESULT_T CommandProcessor_Echo(int echo);
static void EraseChars(int keycount);
static void EchoString(char * p);

// helper functions
static int myisprint(int c);
static void mystrcat(char *dst, char *src);
static char mytolower(char a);
static int mystrnicmp(const char *l, const char *r, size_t n);

static CMDP_T CommandProcessor = {
    CommandProcessor_Init,
    CommandProcessor_Add,
    CommandProcessor_Run,
    CommandProcessor_Echo,
    CommandProcessor_End
};

static RUNRESULT_T Help(char *p);
static RUNRESULT_T History(char *p);
static RUNRESULT_T Echo(char *p);
static RUNRESULT_T Exit(char *p);
//static RUNRESULT_T About(char *p);

static CMD_T HelpMenu = {"Help", "Help or '?' shows this help, 'Help ?' shows more details.", Help, visible};
static CMD_T QuestionMenu = {"?", "Shows this help, '? ?' shows more details.", Help, invisible};
static CMD_T HistoryMenu = {"History", "Show command history", History, visible};
static CMD_T EchoMenu = {"Echo", "Echo [1|on|0|off] turns echo on or off.", Echo, visible};
static CMD_T ExitMenu = {"Exit", "Exits the program", Exit, visible};

/// Gets a handle to the CommandProcessor
///
/// This returns a handle to the CommandProcessor, which then permits
/// access to the CommandProcessor functions.
///
/// @returns handle to the CommandProcessor
///
CMDP_T * GetCommandProcessor(void) {
    return &CommandProcessor;
}


/// History shows the command history
///
/// @param p is a pointer to a string that is ignored
/// @returns runok
///
static RUNRESULT_T History(char *p) {
    int whereInHistory = 0;
    char buf[100];

    cfg.puts("");
    for (whereInHistory = 0; whereInHistory < historyCount; whereInHistory++) {
        sprintf(buf, "  %2i: %s", whereInHistory - historyCount, &historyBuffer[whereInHistory * cfg.bufferSize]);
        cfg.puts(buf);
    }
    sprintf(buf, "  %2i: %s", 0, buffer);
    cfg.puts(buf);
    return runok;
}

/// Turns command prompt echo on and off
///
/// This command is used to turn the command prompt on and off. When
/// running in an interactive mode, it is best to have this one.
/// When driven by another program, off may be the best choice.
///
/// This command also displays the current state of the echo mode.
///
/// @param p is a pointer to a string "on" | "1" | "off" | "0"
/// @returns runok
///
static RUNRESULT_T Echo(char *p) {
    if (*p) {
        if (*p == '1' || mystrnicmp(p, "on", 2) == 0)
            CommandProcessor_Echo(1);
        if (*p == '0' || mystrnicmp(p, "off", 3) == 0)
            CommandProcessor_Echo(0);
    }
    if (cfg.echo)
        cfg.puts("\r\nEcho is on");
    else
        cfg.puts("\r\nEcho is off");
    return runok;
}

static RUNRESULT_T Exit(char *p) {
    (void)p;
    cfg.puts("\r\nbye.");
    return runexit;
}

static RUNRESULT_T Help(char *p) {
    CMDLINK_T *link = head;
    char buffer[100];
    cfg.puts("\r\n");
    //sprintf(buffer, " %-10s: %s", "Command", "Description");
    //cfg.puts(buffer);
    while (link && link->menu) {
        if (link->menu->visible) {
            if (strlen(link->menu->command) + strlen(link->menu->helptext) + 5 < sizeof(buffer)) {
                sprintf(buffer, " %-*s: %s", longestCommand, link->menu->command, link->menu->helptext);
                cfg.puts(buffer);
            }
        }
        link = link->next;
    }
    cfg.puts("");
    return runok;
}


/// CommandMatches is the function that determines if the user is entering a valid
/// command
///
/// This function gets called whenever the user types a printable character or when
/// they press \<enter\>.
/// The buffer of user input is evaluated to determine if the command is legitimate.
/// It also determines if they want to execute the command, or simply evaluate it.
/// Finally, it identifies writes to user provided pointers, the menu item that
/// matches and a pointer to any parameters that the user entered.
///
/// @param buffer is the buffer that the user has entered commands into
/// @param exec indicates the intention to execute the command, if found
/// @param menu is a pointer to a pointer to a menu structure, which is updated based on the command
/// @param params is a pointer to a pointer to the user entered parameters
/// @returns the number of menu picks that match the user entered command
///
static int CommandMatches(char * buffer, int exec, CMD_T **menu, char **params) {
    char *space;
    int compareLength;
    int foundCount = 0;
    CMDLINK_T *link = head;
    char * alternateBuffer;

    if (strlen(buffer)) {  // simple sanity check
        // Try to process the buffer. A command could be "Help", or it could be "Test1 123 abc"
        space = strchr(buffer, ' ');        // if the command has parameters, find the delimiter
        if (space) {
            compareLength = space - buffer;
            space++;        // advance to the char after the space (where the params may start)
        } else {
            compareLength = strlen(buffer);
            space = buffer + compareLength; // points to the NULL terminator
        }
        while (link && link->menu) {
            int cmpResult;

            if (cfg.caseinsensitive) {
                cmpResult = mystrnicmp(buffer, link->menu->command, compareLength);
            } else
                cmpResult = strncmp(buffer, link->menu->command, compareLength);
            if (0 == cmpResult) {  // A match to what they typed
                if (menu) {  // yes, we have a callback
                    *menu = link->menu;    // accessor to the command they want to execute
                    *params = space;
                }
                foundCount++;    // how many command match what they typed so far
            }
            link = link->next;    // follow the link to the next command
        }
        if (foundCount == 1) {
            // If we found exactly one and they expressed an intent to execute that command
            // then we'll rewrite the command to be fully qualified
            if (exec) {  // command wants to execute it, not just validate the command syntax
                // If they type "He 1234 5678", we backup and rewrite as "Help 1234 5678"
                int diff = strlen((*menu)->command) - compareLength;    // e.g. 5 - 3

                // or if they entered it in a case that doesn't match the command exactly
                if (diff > 0 || 0 != strncmp(buffer, (*menu)->command, compareLength)) {
                    char *p = buffer;
                    alternateBuffer = (char *)malloc(cfg.bufferSize);
                    strcpy(alternateBuffer, (*menu)->command);
                    strcat(alternateBuffer, " ");
                    strcat(alternateBuffer, space);
                    EraseChars(strlen(buffer));
                    strcpy(buffer, alternateBuffer);
                    free(alternateBuffer);
                    EchoString(p);
                    *params = strchr(buffer, ' ');        // if the command has parameters, find the delimiter
                    if (*params) {
                        (*params)++;        // advance to the char after the space (where the params may start)
                    } else {
                        compareLength = strlen(buffer);
                        *params = buffer + compareLength; // points to the NULL terminator
                    }
                }
            }
        }
    }
    return foundCount;
}


/// Init is the first function to call to configure the CommandProcessor.
///
/// This function has a number of parameters, which make the CommandProcessor
/// quite flexible.
///
/// @param SignOnBanner function, which is used as a signon banner
/// @param config enables various default menu items, based on the bit values, combine the following:
///   \li CFG_ENABLE_TERMINATE - enables the Exit command
///   \li CFG_ENABLE_SYSTEM    - enables system commands Echo, Help, etc.
///   \li CFG_ECHO_ON          - initialize with echo on
///   \li CFG_CASE_INSENSITIVE - Command Parser is case insensitive
/// @param maxCmdLen sizes the buffer, and is the maximum number of characters in a single
///        command, including all command arguments
/// @param kbhit is a user provided function to detect if a character is available for the CommandProcessor,
///        and when using standard io, you can typically use kbhit, or _kbhit as your system provides.
/// @param getch is a user provided function that provides a single character to the CommandProcessor
/// @param putch is a user provided function that permits the CommandProcessor to output a character
/// @param puts is a user provided function that permits the CommandProcessor to output a string
///        to which is automatically appended a \\n
/// @returns INITRESULT_T to indicate if the init was successful or failed
///
INITRESULT_T CommandProcessor_Init(
    CMD_T (*SignOnBanner),
    CONFIG_T config,
    int maxCmdLen,
    int numInHistory,
    int (*kbhit)(void),
    int (*getch)(void),
    int (*putch)(int ch),
    int (*puts)(const char * s)
) {
    if (SignOnBanner) {
        CommandProcessor.Add(SignOnBanner);
        cfg.SignOnBanner = SignOnBanner;
        cfg.showSignOnBanner = 1;
    }
    if (maxCmdLen < 6)
        maxCmdLen = 6;
    buffer = (char *)malloc(maxCmdLen);            // users often error by one, so we'll be generous
    historyDepth = numInHistory;
    historyBuffer = (char *)malloc(historyDepth * maxCmdLen);
    cfg.bufferSize = maxCmdLen;
    if (buffer && historyBuffer) {
        if (config & CFG_ENABLE_SYSTEM) {
            CommandProcessor.Add(&QuestionMenu);
            CommandProcessor.Add(&HelpMenu);
            CommandProcessor.Add(&HistoryMenu);
            CommandProcessor.Add(&EchoMenu);
        }
        if (config & CFG_ENABLE_TERMINATE)
            CommandProcessor.Add(&ExitMenu);
        //if (addDefaultMenu & 0x0002)
        //    CommandProcessor.Add(&AboutMenu);
        cfg.caseinsensitive = (config & CFG_CASE_INSENSITIVE) ? 1 : 0;
        cfg.echo = (config & CFG_ECHO_ON) ? 1 : 0;
        cfg.kbhit = kbhit;
        cfg.getch = getch;
        cfg.putch = putch;
        cfg.puts = puts;
        return initok;
    } else
        return initfailed;
}


/// Add a command to the CommandProcessor
///
/// This adds a command to the CommandProcessor. A command has several components
/// to it, including the command name, a brief description, the function to
/// activate when the command is entered, and a flag indicating if the command
/// should show up in the built-in help.
///
/// @param menu is the menu to add to the CommandProcessor
/// @returns addok if the command was added
/// @returns addfail if the command could not be added (failure to allocate memory for the linked list)
///
ADDRESULT_T CommandProcessor_Add(CMD_T * menu) {
    CMDLINK_T *ptr;
    CMDLINK_T *prev;
    CMDLINK_T *temp;

    if (strlen(menu->command) > longestCommand)
        longestCommand = strlen(menu->command);

    // Allocate the storage for this menu item
    temp = (CMDLINK_T *)malloc(sizeof(CMDLINK_T));
    if (!temp)
        return addfailed;            // something went really bad
    temp->menu = menu;
    temp->next = NULL;

    prev = ptr = head;
    if (!ptr) {
        head = temp;            // This installs the very first item
        return addok;
    }
    // Search alphabetically for the insertion point
    while (ptr && mystrnicmp(ptr->menu->command, menu->command, strlen(menu->command)) < 0) {
        prev = ptr;
        ptr = ptr->next;
    }
    if (prev == head) {
        head = temp;
        head->next = prev;
    } else {
        prev->next = temp;
        prev = temp;
        prev->next = ptr;
    }
    return addok;
}

static void EchoString(char *p) {
    while (*p)
        cfg.putch(*p++);
}

static void EraseChars(int keycount) {
    while (keycount--) {
        cfg.putch(0x08);    // <bs>
        cfg.putch(' ');
        cfg.putch(0x08);
    }
}


static int ProcessComplexSequence(int c) {
    switch (c) {
        case 0x42:
        case 0x50:    // down arrow - toward the newest (forward in time)
            // if there is anything in the history, copy it out
            if (historyCount && whereInHistory < historyCount) {
                char *p;

                EraseChars(keycount);
                p = strcpy(buffer, &historyBuffer[whereInHistory * cfg.bufferSize]);
                EchoString(p);
                keycount = strlen(buffer);
                whereInHistory++;
            }
            c = 0;
            break;
        case 0x41:
        case 0x48:    // up arrow - from newest to oldest (backward in time)
            // same as escape
            if (historyCount && --whereInHistory >= 0) {
                char *p;

                EraseChars(keycount);
                p = strcpy(buffer, &historyBuffer[whereInHistory * cfg.bufferSize]);
                EchoString(p);
                keycount = strlen(buffer);
                c = 0;
            } else {
                whereInHistory = 0;
                c = 0x1B;
            }
            break;
        default:
            // ignore this char
            c = 0;
            break;
    }
    leadinChar = 0;
    return c;
}


static RUNRESULT_T ProcessStandardSequence(int c) {
    int foundCount = 0;
    CMD_T *cbk = NULL;
    char * params = NULL;
    RUNRESULT_T val = runok;

    // Process Character
    switch (c) {
        case 0:
            // null - do nothing
            break;
        case 0x5B:
            // ANSI (VT100) sequence
            // <ESC>[A is up
            // <ESC>[B is down
            // <ESC>[C is right
            // <ESC>[D is left
            leadinChar = 1;
            break;
        case 0xE0:
            // Windows Command Shell (DOS box)
            // Lead-in char
            // 0xE0 0x48 is up arrow
            // 0xE0 0x50 is down arrow
            // 0xE0 0x4B is left arrow
            // 0xE0 0x4D is right arrow
            leadinChar = 1;
            break;
        case 0x09:    // <TAB> to request command completion
            if (1 == CommandMatches(buffer, FALSE, &cbk, &params)) {
                size_t n;
                char *p = strchr(buffer, ' ');
                if (p)
                    n = p - buffer;
                else
                    n = strlen(buffer);
                if (n < strlen(cbk->command)) {
                    p = cbk->command + strlen(buffer);
                    mystrcat(buffer, p);
                    keycount = strlen(buffer);
                    EchoString(p);
                    //cfg.printf("%s", p);
                }
            }
            break;
        case 0x1b:    // <ESC> to empty the command buffer
            EraseChars(keycount);
            keycount = 0;
            buffer[keycount] = '\0';
            break;
        case '\x08':    // <bs>
            if (keycount) {
                buffer[--keycount] = '\0';
                EraseChars(1);
            } else
                cfg.putch(0x07);    // bell
            break;
        case '\r':
        case '\n':
            if (strlen(buffer)) {
                foundCount = CommandMatches(buffer, TRUE, &cbk, &params);
                if (foundCount == 1) {
                    val = (*cbk->callback)(params);        // Execute the command
                    if (mystrnicmp(buffer, (const char *)&historyBuffer[(historyCount-1) * cfg.bufferSize], strlen(&historyBuffer[(historyCount-1) * cfg.bufferSize])) != 0) {
                        // not repeating the last command, so enter into the history
                        if (historyCount == historyDepth) {
                            int i;
                            historyCount--;
                            for (i=0; i<historyCount; i++)
                                strcpy(&historyBuffer[i * cfg.bufferSize], &historyBuffer[(i+1) * cfg.bufferSize]);
                        }
                        strcpy(&historyBuffer[historyCount * cfg.bufferSize], buffer);
                        whereInHistory = historyCount;
                        historyCount++;
                    }
                } else if (foundCount > 1)
                    cfg.puts(" *** non-unique command ignored      try 'Help' ***");
                else if (foundCount == 0)
                    cfg.puts(" *** huh?                            try 'Help' ***");
            } else
                cfg.puts("");
            keycount = 0;
            buffer[keycount] = '\0';
            showPrompt = TRUE;        // forces the prompt
            break;
        default:
            // any other character is assumed to be part of the command
            if (myisprint(c) && keycount < cfg.bufferSize) {
                buffer[keycount++] = (char)c;
                buffer[keycount] = '\0';
                if (CommandMatches(buffer, FALSE, &cbk, &params))
                    cfg.putch(c);
                else {
                    buffer[--keycount] = '\0';
                    cfg.putch(0x07);    // bell
                }
            } else
                cfg.putch(0x07);    // bell
            break;
    }
    return val;
}


#if 0
static void PutCharToHex(int c) {
    int upper = c >> 4;
    int lower = c & 0x0F;

    cfg.putch('[');
    if (upper >= 10)
        cfg.putch(upper - 10 + 'A');
    else
        cfg.putch(upper + '0');
    if (lower >= 10)
        cfg.putch(lower - 10 + 'A');
    else
        cfg.putch(lower + '0');
    cfg.putch(']');
}
#endif

/// Run the CommandProcessor
///
/// This will peek to see if there is a keystroke ready. It will pull that into a
/// buffer if it is part of a valid command in the command set. You may then enter
/// arguments to the command to be run.
///
/// Primitive editing is permitted with <bs>.
///
/// When you press <enter> it will evaluate the command and execute the command
/// passing it the parameter string.
///
/// @returns runok if the command that was run allows continuation of the CommandProcessor
/// @returns runfail if the command that was run is asking the CommandProcessor to exit
///
RUNRESULT_T CommandProcessor_Run(void) {
    RUNRESULT_T val = runok;            // return true when happy, false to exit the prog

    if (cfg.showSignOnBanner) {
        cfg.SignOnBanner->callback("");
        cfg.showSignOnBanner = 0;
    }
    if (showPrompt && cfg.echo) {
        cfg.putch('>');
        showPrompt = FALSE;
    }
    if (cfg.kbhit()) {
        int c = cfg.getch();
        //PutCharToHex(c);      // a debug utility
        if (leadinChar) {
            // some previous character was a lead-in to a more complex sequence
            // to be processed
            c = ProcessComplexSequence(c);
        }
        ProcessStandardSequence(c);
    }
    return val;
}

static RUNRESULT_T CommandProcessor_Echo(int echo) {
    cfg.echo = echo;
    return runok;
}

/// End the CommandProcessor by freeing all the memory that was allocated
///
/// @returns runok
///
RUNRESULT_T CommandProcessor_End(void) {
    CMDLINK_T *p = head;
    CMDLINK_T *n;

    do {
        n = p->next;
        free(p);            // free each of the allocated links to menu items.
        p = n;
    } while (n);
    free(buffer);            // finally, free the command buffer
    buffer = NULL;            // flag it as deallocated
    return runok;
}


// Helper functions follow. These functions may exist in some environments and
// not in other combinations of libraries and compilers, so private versions
// are here to ensure consistent behavior.

/// mytolower exists because not all compiler libraries have this function
///
/// This takes a character and if it is upper-case, it converts it to
/// lower-case and returns it.
///
/// @param a is the character to convert
/// @returns the lower case equivalent to a
///
static char mytolower(char a) {
    if (a >= 'A' && a <= 'Z')
        return (a - 'A' + 'a');
    else
        return a;
}

/// mystrnicmp exists because not all compiler libraries have this function.
///
/// Some have strnicmp, others _strnicmp, and others have C++ methods, which
/// is outside the scope of this C-portable set of functions.
///
/// @param l is a pointer to the string on the left
/// @param r is a pointer to the string on the right
/// @param n is the number of characters to compare
/// @returns -1 if l < r
/// @returns 0 if l == r
/// @returns +1 if l > r
///
static int mystrnicmp(const char *l, const char *r, size_t n) {
    int result = 0;

    if (n != 0) {
        do {
            result = mytolower(*l++) - mytolower(*r++);
        } while ((result == 0) && (*l != '\0') && (--n > 0));
    }
    if (result < -1)
        result = -1;
    else if (result > 1)
        result = 1;
    return result;
}

/// mystrcat exists because not all compiler libraries have this function
///
/// This function concatinates one string onto another. It is generally
/// considered unsafe, because of the potential for buffer overflow.
/// Some libraries offer a strcat_s as the safe version, and others may have
/// _strcat. Because this is needed only internal to the CommandProcessor,
/// this version was created.
///
/// @param dst is a pointer to the destination string
/// @param src is a pointer to the source string
/// @returns nothing
///
static void mystrcat(char *dst, char *src) {
    while (*dst)
        dst++;
    do
        *dst++ = *src;
    while (*src++);
}

/// myisprint exists because not all compiler libraries have this function
///
/// This function tests a character to see if it is printable (a member
/// of the standard ASCII set).
///
/// @param c is the character to test
/// @returns TRUE if the character is printable
/// @returns FALSE if the character is not printable
///
static int myisprint(int c) {
    if (c >= ' ' && c <= '~')
        return TRUE;
    else
        return FALSE;
}
