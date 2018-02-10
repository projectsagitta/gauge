/// @file CommandProcessor.h defined the interface to the CommandProcessor
///
/// @mainpage The CommandProcessor
/// 
/// The CommandProcessor is the interface to install a run-time menu into an embedded system.
/// This contains the complete interface to the CommandProcessor.
///
/// @version 1.05
///
/// @note The CommandProcessor is text-based, because it is intended to interact with a
///       user.
///
/// The menu is designed to be interactively accessed, perhaps via a console interface
/// or a serial port. The actual interface to the user is provided by the application 
/// during initialization, so it can be connected to a serial port, or it could
/// be interface to CAN, telnet, or by some other method. 
///
/// The CommandProcessor has a few special features:
/// \li If the minimum number of characters of a command have been entered, the
///   user does not have to type the entire command. (e.g. 'He' will execute 
///   the command for 'Help', if no other command beings with 'He').
/// \li If the user does not type the entire set of characters, the command
///   will be rewritten to the output device with the entire command word.
/// \li The user is not permitted to enter an incorrect command (e.g. If 'Help'
///   is the only command started with 'Hel', the user cannot enter 'Heu'.
///   The CommandProcessor will trap the 'u' and issue a beep).
/// \li Simple editing of parameters to commands is permitted with \<bs\> to 
///   erase incorrect text.
/// \li Tab completion of a command is available - so long as the user has
///      typed at least the minimum number of unique characters. (e.g. 'He\<tab\>'
///   will be replaced with 'Help')
///    \li Command cancellation is available - just enter the \<esc\> key and
///   the buffer is erased.
/// \li The user is not permitted to enter text longer than the defined buffer,
///   to avoid buffer overrun and the possible memory damaging results.
///
/// The CommandProcessor is designed as a set of C functions, which makes it
/// reusable in more systems (as C++ compilers are not always available for
/// all micros).
///
/// Example:
/// @code
/// extern "C" {
/// #include "CommandProcessor.h"
/// }
/// 
/// RUNRESULT_T SignOnBanner(char *p);
/// const CMD_T SignOnBannerCmd = {
///       "About", "About this program ('About ?' for more details)", 
///       SignOnBanner, invisible};
/// 
/// RUNRESULT_T Who(char *p);
/// const CMD_T WhoCmd = {
///       "who", "Shows who is logged on, or 'who id' for specifics", 
///       Who, visible};
///
/// RUNRESULT_T SignOnBanner(char *p)
/// {
///     puts("\r\nThis great program was built " __DATE__ " " __TIME__ ".");
///     if (*p == '?')
///        puts("\r\nMore details shown here.\r\n");
///     return runok;
/// }
/// RUNRESULT_T Who(char *p)
/// {
///     printf("\r\nwho...\r\n");
///     if (*p)
///         printf(" Sorry, no help for [%s]\r\n", p);
///     return runok;
/// }
/// 
/// int main(int argc, char* argv[])
/// {
///     CMDP_T * cp = GetCommandProcessor();
///     cp->Init(&SignOnBanner, 
///              CFG_ENABLE_TERMINATE | CFG_ENABLE_SYSTEM, 
///              50, _kbhit, _getch, _putch, printf);
///     cp->Add(&WhoCmd);
/// 
///     while (cp->Run())
///     {
///         ;
///     }
///     cp->End();
///     return 0;
/// }
/// @endcode
///
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
/// @note
/// History
/// v1.05 20111030
/// \li Added support for VT100 cursor code for up/down history access
/// \li Fixed a bug related to not entering any parameters - causing it to think
///         there were parameters anyway.
/// v1.04 1 October 2011
/// \li Added configurable command line history for easy recall of previous commands
/// \li Clean up dead-code
/// v1.03 29 May 2011
/// \li Slightly improved internal documentation. No external interfaces affected.
/// v1.02 2 May 2011
/// \li Track the longest command when added, so that the help printout
///         is more nicely formatted.
/// v1.01 22 April 2011
/// \li Moving 'About' content into the extended help, 
///         to free 'About' for application code that uses this library.
/// \li Altered the _init api to permit a signon banner of the users choice
///         and a config parameter for other features.
/// 
/// v1.0  March 2011
/// \li Initial version
/// 
#ifndef COMMANDPROCESSOR_H
#define COMMANDPROCESSOR_H

#define VERSION "1.05"

#ifndef TRUE
#define TRUE 1        ///< Definition for TRUE, if not already provided
#define FALSE 0        ///< Definition for FALSE, if not already provided
#endif

/// @brief This type determines if menu items are visible to the Help system, or hidden.
/// @details This is used in the definition of the menu item.
typedef enum 
{
    invisible,        ///< use this value to have invisible (hidden) a menu in the Help
    visible            ///< use this value to have visible a menu in the Help
} VISIBLE_T;        ///< This determines if menu items are made visible in the Help system.

/// Callbacks that are executed return a value to indicate if the menu
/// should remain active
typedef enum
{
    runexit,        ///< use this return value to cause the menu (perhaps the program) to exit
    runok            ///< use this return value to keep the menu running
} RUNRESULT_T;

/// Adding items to the menu can succeed, or fail.
typedef enum
{
    addfailed,        ///< this indicates the menu was not added (usually failure to allocate memory)
    addok            ///< this indicates the menu was successfully added
} ADDRESULT_T;

/// Initialization can succeed, or fail.
typedef enum
{
    initfailed,        ///< this indicates that the menu system was not initialized (usually failure to allocate memory)
    initok            ///< this indicates that the menu system was successfully initialized
} INITRESULT_T;

/// Configuration options to control startup and some runtime behavior
///
/// Permissible values are created by combining
/// \li CFG_ENABLE_TERMINATE
/// \li CFG_ENABLE_SYSTEM   
/// \li CFG_ECHO_ON          
/// \li CFG_CASE_INSENSITIVE
typedef unsigned long CONFIG_T;

#define CFG_ENABLE_TERMINATE 0x0001 ///<- Enable the exit option
#define CFG_ENABLE_SYSTEM    0x0002 ///<- Enable various system options (help, etc)
#define CFG_ECHO_ON          0x2000 ///<- Initialize with command prompt Echo on
#define CFG_CASE_INSENSITIVE 0x4000 ///<- Enable case insensitive command entry


/// This is the type for the basic callback, when a menu pick is activated.
///
/// The callback function is executed when a command is entered on the menu and \<enter\>
/// is signaled. If there is any additional text entered on the commandline, it is
/// passed to the callback.
///
/// example:
///        "Test1 ab c 123 567"
/// If "Test1" is a valid command, the corresponding function would be called
///    passing to that function the string "ab c 123 567". Note that the delimiter space
///    was removed.
/// 
/// @param p is a pointer to a character string
/// @returns RUNRESULT_T to indicate if the CommandProcessor should continue
///
typedef RUNRESULT_T (*MENU_CALLBACK)(char *p);

/// This defines the type for a single item to be added to the CommandProcessor menu.
///
/// This is defined in the application code, and a pointer to this item is passed to the
/// CommandProcessor to add this item to the menu system.
///
/// example:
/// @code
/// const CMD_T WhoCmd = {"who", "Shows who is logged on, or 'who id' for specifics", Who, visible};
/// @endcode
///
typedef const struct 
{
    char * command;                ///< a pointer to the command to match (e.g. 'Help')
    char * helptext;            ///< a pointer to some text to show when user types 'Help'
    MENU_CALLBACK callback;        ///< the function to call when user enters this command
    VISIBLE_T visible;            ///< a flag that determines if this command is visible in Help.
} CMD_T;

/// This is the CommandProcessor interface from the user application.
///
/// The user aquires a handle to this set of functions with the GetCommandProcessor command.
/// After this, the user may then initialize the CommandProcessor, add items to the menu,
/// cause the CommandProcessor to run periodically, and if need be the application can end
/// the CommandProcessor.
///
typedef const struct
{
    /// Init is the first function to call to configure the CommandProcessor.
    ///
    /// This function has a number of parameters, which make the CommandProcessor quite flexible.
    ///
    /// @param SignOnBanner function, which is used as a signon banner
    /// @param config enables various default menu items, based on the bit values, combine the following:
    ///   \li CFG_ENABLE_TERMINATE - enables the Exit command
    ///   \li CFG_ENABLE_SYSTEM    - enables system commands Echo, Help, etc.
    ///   \li CFG_ECHO_ON          - initialize with echo on
    ///   \li CFG_CASE_INSENSITIVE - Command Parser is case insensitive
    /// @param maxCmdLen sizes the buffer, and is the maximum number of characters in a single
    ///        command, including all command arguments
    /// @param historyLen sets the number of items that can be recalled from history
    /// @param kbhit is a user provided function to detect if a character is available for the CommandProcessor,
    ///        and when using standard io, you can typically use kbhit, or _kbhit as your system provides.
    /// @param getch is a user provided function that provides a single character to the CommandProcessor
    /// @param putch is a user provided function that permits the CommandProcessor to output a character
    /// @param puts is a user provided function that permits the CommandProcessor to output a string
    ///        to which is automatically appended a \\n
    /// @returns INITRESULT_T to indicate if the init was successful or failed
    ///
    INITRESULT_T (*Init)(
        CMD_T *SignOnBanner,
        CONFIG_T config,
        int maxCmdLen,
        int historyLen,
        int (*kbhit)(void),
        int (*getch)(void),
        int (*putch)(int ch),
        int (*puts)(const char * s)
        );

    /// Add is called to add an item to the CommandProcessor menu
    ///
    ///    This passes in a reference to a user provided CMD_T item, which is
    ///    added to the menu system.
    ///
    /// @param m is a pointer to the user provided menu
    /// @returns ADDRESULT_T to indicate if the add was successful or failed
    ///
    ADDRESULT_T (*Add)(CMD_T * m);

    /// Run is the primary runtime entry point for the CommandProcessor.
    ///
    /// This function should be called periodically - fast enough not to miss user input.
    /// This function always returns, so if not character is available for the CommandProcessor
    /// it will return very fast. If there is a character (as detected by the kbhit callback),
    /// then it will process that character and determine what to do. It may then execute one
    /// of the menu functions. In this case, CPU cycles spent are based on the function 
    /// being executed.
    /// 
    /// @returns RUNRESULT_T to indicate if the CommandProcessor should remain active or if the 
    ///            command that was executed is requesting the CommandProcessor to exit.
    ///
    RUNRESULT_T (*Run)(void);
    
    /// Echo command permits turning the echo on and off
    ///
    /// When interactive with the user, it is best to have echo on, so they can see
    /// the prompt, but if this is simply slaved to another program, then the echo
    /// might need to be off to best manage the stream.
    ///
    /// @param echo turns the echo on (non-zero) or off (zero)
    /// @returns RUNRESULT_T to indicate if the CommandProcessor should remain active or if the 
    ///            command that was executed is requesting the CommandProcessor to exit.
    ///
    RUNRESULT_T (*Echo)(int echo);

    /// End if the function to be called when you want to gracefully end the CommandProcessor.
    ///
    ///    Calling this function causes the CommandProcessor to free any memory that was previously
    /// allocated by the Init and Add functions.
    RUNRESULT_T (*End)(void);            ///< Called to shutdown the processor
} CMDP_T;


/// The CommandProcessor is the interface to install a run-time menu into an embedded system.
/// This contains the complete interface to the CommandProcessor.
///
/// @version 1.05
///
/// @note The CommandProcessor is text-based, because it is intended to interact with a
///       user.
///
/// The menu is designed to be interactively accessed, perhaps via a console interface
/// or a serial port. The actual interface to the user is provided by the application 
/// during initialization, so it can be connected to a serial port, or it could
/// be interface to CAN, telnet, or by some other method. 
///
/// The CommandProcessor has a few special features:
/// \li If the minimum number of characters of a command have been entered, the
///   user does not have to type the entire command. (e.g. 'He' will execute 
///   the command for 'Help', if no other command beings with 'He').
/// \li If the user does not type the entire set of characters, the command
///   will be rewritten to the output device with the entire command word.
/// \li The user is not permitted to enter an incorrect command (e.g. If 'Help'
///   is the only command started with 'Hel', the user cannot enter 'Heu'.
///   The CommandProcessor will trap the 'u' and issue a beep).
/// \li Simple editing of parameters to commands is permitted with \<bs\> to 
///   erase incorrect text.
/// \li Tab completion of a command is available - so long as the user has
///      typed at least the minimum number of unique characters. (e.g. 'He\<tab\>'
///   will be replaced with 'Help')
///    \li Command cancellation is available - just enter the \<esc\> key and
///   the buffer is erased.
/// \li The user is not permitted to enter text longer than the defined buffer,
///   to avoid buffer overrun and the possible memory damaging results.
///
/// The CommandProcessor is designed as a set of C functions, which makes it
/// reusable in more systems (as C++ compilers are not always available for
/// all micros).
///
/// Example:
/// @code
/// extern "C" {
/// #include "CommandProcessor.h"
/// }
/// 
/// RUNRESULT_T About(char *p);
/// const CMD_T AboutCmd = {"About", "About this program", About, invisible};
/// RUNRESULT_T Who(char *p);
/// const CMD_T WhoCmd = {"who", "Shows who is logged on, or 'who id' for specifics", Who, visible};
/// 
/// RUNRESULT_T About(char *p)
/// {
///     (void)p;
///     puts("\r\nThis program does really good things for the user.\r\n");
/// }
///
/// RUNRESULT_T Who(char *p)
/// {
///     printf("\r\nwho...\r\n");
///     if (*p)
///         printf(" Sorry, no help for [%s]\r\n", p);
///     return runok;
/// }
/// 
/// int main(int argc, char* argv[])
/// {
///     CMDP_T * cp = GetCommandProcessor();
///     cp->Init(AboutCmd, CFG_ENABLE_TERMINATE | CFG_ENABLE_SYSTEM | CFG_SIGNON_BANNER, 
///              50, 
///              _kbhit, _getch, _putch, printf);
///     cp->Add(&WhoCmd);
/// 
///     while (cp->Run())
///     {
///         ;
///     }
///     cp->End();
///     return 0;
/// }
/// @endcode
///
/// @note Copyright &copy; 2011 by Smartware Computing, all rights reserved.
///       This program may be used by others as long as this copyright notice
///       remains intact.
/// @author David Smart
///
/// GetCommandProcessor is called to get a handle to the CommandProcessor itself.
///
/// Call this function to get a handle to the CommandProcessor. After this is done, then
/// you can use that handle to activate the CommandProcessor methods.
///
/// example:
/// @code
///     CMDP_T * cp = GetCommandProcessor();
///     cp->Init(AboutCmd, CFG_ENABLE_TERMINATE | CFG_ENABLE_SYSTEM | CFG_SIGNON_BANNER, 
///              50,
///              _kbhit, _getch, _putch, printf);
/// @endcode
/// 
/// @returns CMDP_T a handle to the CommandProcessor
///
#ifdef WIN32
extern CMDP_T * GetCommandProcessor(void);
#else // This is necessary for the mbed - not sure why.
extern CMDP_T * GetCommandProcessor(void);
#endif

#endif // COMMANDPROCESSOR_H
