/*** include ***/

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)                                // The CTRL_KEY macro bitwise-ANDs a character with the value 00011111
                                                                // It mirrors what the ctrl key does in the terminal: it strips bits 5 and 6 from whatever key you pressed in the combination with ctrl

/*** data ***/
struct editorConfig{
    int screenrows, screencols;
    struct termios original_termios;                            // Original terminal state
}configuration;

/*** Terminal ***/

void die( const char* s)                                        // function used for error handling
{
    write(STDOUT_FILENO, "\x1b[2J", 4);                         // This function is used to clear the screen when we exit. The 2J argument clears the whole screen.
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

void disableRawMode()
{
    if( tcsetattr(STDIN_FILENO, TCSAFLUSH, &configuration.original_termios) == -1 ) 
        die("tcsetattr");                                        //when exiting, we reset the terminal to its original state
}

void enableRawMode()
{
    if( tcgetattr(STDIN_FILENO, &configuration.original_termios) == -1 )       // read the attributes in a struct 
        die("tcgetattr");                  
    atexit(disableRawMode);                                      // register our disableRawMode() function to be called automatically when the program exits 
    
    struct termios to_raw = configuration.original_termios;                    // copy of the initial state of the terminal
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

int getCursorPosition(int* rows, int* cols)
{
    char buffer[32];
    unsigned int i = 0;
    // (void)* rows;
    // (void)* cols;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)                   // The n command can be used to query the terminal for status information. I want to give it an argument of 6 to ask for the cursor position.
        return -1;
    
    while( i < sizeof(buffer) - 1 )                               // I store in the buffer the output of that escape sequence.
    {
        if( read(STDIN_FILENO, &buffer[i], 1) != 1 )
            break;
        if( buffer[i] == 'R' )
            break;
        i++;
    }

    buffer[i] = '\0';                                             // In order print it on the screen i store at the end the terminator. It is needed by the printf function

    if( buffer[0] != '\x1b' || buffer[1] != '[' )
        return -1;
    if( sscanf(&buffer[2], "%d;%d", rows, cols) != 2 )            // I split the buffer into to and store the two parts into the two variables.
        return -1;
    return 0;
}

int getWindowSize(int* rows, int* cols)                           // This function gets the initial mesuremenets of the termial window. for more check https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
{
    /*ATTENTION! This will fail on some systems, so i made a fllback method just in case */
    struct winsize ws;

    if( ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 ) // ioctl takes the dimensions of the device and places them into the winsize struct
    {
        if( write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12 ) return -1;// we are sending to escape sequences one after the other.
        //C - moves the cursor to the right with 999 spaces to be sure that it reaches the rightest most point. ! It will not go over the boundry of the terminal
        //B - moves the cursor down with 999 spaces --||--
        return getCursorPosition(rows, cols);                                                 // TIOCGWINSZ - Terminal Input/Output Control Get Windows Size
    }
    else{
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    }
    return 0;
}

/*** dynamic string and writing buffer ***/

struct appendBuffer{                                               // we use this to not call so many writes each time we refresh the screen 
    char* seq;
    int len;
};

#define aBuf_init {NULL, 0}

void BufferAdder(struct appendBuffer* ab, const char* s, int len)
{
    char* new = realloc(ab->seq, ab->len + len);                   // reallocate somewhere in the memory where there is enough space for the whole string

    if( new == NULL ) return;
    memcpy(&new[ab->len], s, len);                                 // copy at the end of the existing string the new string 
    ab->seq = new;
    ab->len += len;
}

void BufferFree(struct appendBuffer* ab)
{
    free(ab->seq);
}

/*** output ***/

void editorClearScreen(struct appendBuffer* buffer)
{
    BufferAdder(buffer, "\x1b[2J", 4);                            // \x1b is an escape character ( 27 in decimal ). The escape sequence tells the terminal to clear the whole screen.
                                                                   // for more visit https://vt100.net/docs/vt100-ug/chapter3.html#ED
}

void repositionCursor(struct appendBuffer* buffer)
{
    BufferAdder(buffer, "\x1b[H", 3);                             // Uses the H command to reposition the cursor. It takes two arguments which are implicitly 1 and 1 ( row and column ).
                                                                   // So \x1b[H can also be written as \x1b[1;1H as here the rows and columns start from 1 not 0.
}

void editorDrawRows(struct appendBuffer* buffer)
{
    for( int i = 0; i < configuration.screenrows; i ++ )
    {
        BufferAdder(buffer, "~", 1);
        if( i < configuration.screenrows - 1 )
            BufferAdder(buffer, "\r\n", 2);
    }
}

void refreshScreen()
{
    struct appendBuffer buffer = aBuf_init;

    editorClearScreen(&buffer);
    repositionCursor(&buffer);
    editorDrawRows(&buffer);
    BufferAdder(&buffer, "\x1b[H", 3);
    write(STDOUT_FILENO, buffer.seq, buffer.len);
    BufferFree(&buffer);
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
            write(STDOUT_FILENO, "\x1b[2J", 4);                    // Again clear the screen
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/

void initEditor()
{
    if( getWindowSize(&configuration.screenrows, &configuration.screencols) == -1 )
        die("getWidnowSize");
}

int main()
{
    enableRawMode();
    //we now want to be able to take input from the users keyboard
    initEditor();

    while(1)                                            //we changed such that the terminal is not waiting for some input
    {
        refreshScreen();
        editorProcessKeypress();
    }        
    return 0;
}
