/*** include ***/

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)                                // The CTRL_KEY macro bitwise-ANDs a character with the value 00011111
                                                                // It mirrors what the ctrl key does in the terminal: it strips bits 5 and 6 from whatever key you pressed in the combination with ctrl

/*** data ***/
struct termios original_termios;

void die( const char* s)                                        // function used for error handling
{
    perror(s);
    exit(1);
}

void disableRawMode()
{
    if( tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1 ) 
        die("tcsetattr");                                        //when exiting, we reset the terminal to its original state
}

void enableRawMode()
{
    if( tcgetattr(STDIN_FILENO, &original_termios) == -1 )       // read the attributes in a struct 
        die("tcgetattr");                  
    atexit(disableRawMode);                                      // register our disableRawMode() function to be called automatically when the program exits 
    
    struct termios to_raw = original_termios;                    // copy of the initial state of the terminal
    //The ECHO feature causes each key you type to be printed to the terminal, so you can see what you’re typing. 

    
    to_raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);                                  // c_iflag responsible for input
    //IXON - I comes from input flag and XON is a combination of the two things we want to stop Ctrl-S = XOFF and Ctrl-Q = XON
    //ICRNL - I stands for “input flag”, CR stands for “carriage return”, and NL stands for “new line”. It turns out that the terminal is helpfully translating any carriage returns (13, '\r') inputted by the user into newlines (10, '\n')
    //see more at https://www.man7.org/linux/man-pages/man3/termios.3.html
    
    to_raw.c_lflag &= ~( ECHO | ICANON | IEXTEN| ISIG );        // change the attribute by hand + c_lglah = dumping ground for other state
    //ECHO=00000000000000000000000000001000 in binary. It is a bitflag
    //ICANON is not an input flag even though the majority of input flags start with I. It is a local flag in c_lflag
    //ISIG - flag responsible for SIGINT and SIGTST. I want to be able to exit the text editor using only the x! combination
    //IEXTEN - flag responsible for the CTRL-V combination
    
    to_raw.c_oflag &= ~(OPOST);                                  // c_oflag responsible for output
    //OPOST - O means output flag an POST stands for "post processing of output". We will turn off all output processing feature by turning off this flag.
    
    to_raw.c_cflag |= (CS8);                                     // c_cflag used to set the character size 
    //CS8 - character size mask. It sets the character size (CS) to 8 bits per byte.
    
    to_raw.c_cc[VMIN] = 0;                                       // c_cc - control characters which control various terminal settings
    to_raw.c_cc[VTIME] = 1;
    //VMIN - value sets the minimum number of bytes of input needed before read() can return
    //VTIME - value sets the maximum amount of time to wait before read() returns. It is in tenths of a second, so we set it to 100 milliseconds.
    if( tcsetattr(STDIN_FILENO, TCSAFLUSH, &to_raw) == -1 )      // set back the attributes
        die("tcsetattr");
    //With this part, we no longer see on the screen the keys we pressed
}

char editorReadKey()
{
    //function used to read characters. It waits for a keypress and than it returns it.
    int nread;
    char char_read;
    while( (nread = read(STDIN_FILENO, &char_read, 1)) != 1 )
    {
        if( nread == -1 && errno != EAGAIN )
            die("read");
    }
    return char_read;
}

/*** output ***/

void editorClearScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
}

/*** input ***/

void editorProcessKeypress()
{
    //this function processes the key pressed by the 
    //it maps keys combination to various editor functions 
    char char_read = editorReadKey();

    switch(char_read)
    {
        case CTRL_KEY('x'):
            exit(0);
            break;
    }
}

/*** init ***/

int main()
{
    enableRawMode();
    //we now want to be able to take input from the users keyboard
              
    while(1)                                            //we changed such that the terminal is not waiting for some input
    {
        editorClearScreen();
        editorProcessKeypress();                        // function taht deals with the input
    }        
    return 0;
}
