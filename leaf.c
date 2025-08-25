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
#define LEAF_VERSION "0.0.1"

enum editorKey{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/
struct editorConfig{
    int screenrows, screencols;
    int cursorX, cursorY;
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

int editorReadKey()
{
    //function used to read characters. It waits for a keypress and than it returns it.
    int nread;
    char char_read;
    while( (nread = read(STDIN_FILENO, &char_read, 1)) != 1 )
    {
        if( nread == -1 && errno != EAGAIN )
            die("read");
    }
    
    if( char_read == '\x1b' )                   // Pressing an arrow key sends multiple bytes as input to our program. These bytes are in the form of an escape sequence that starts with '\x1b', '[', followed by an 'A', 'B', 'C', or 'D' depending on which of the four arrow keys was pressed.
    {
        char seq[3];   
        if( read(STDIN_FILENO, &seq[0], 1) != 1 )
            return '\x1b';
        if( read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if( seq[0] == '[' )
        {
            if( seq[1] >= '0' && seq[1] <= '9' )
            {
                if( read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if( seq[2] == '~')
                {
                    switch(seq[1])
                    {
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            }
            else
            {
                switch(seq[1])
                {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }
            }
        }
        return '\x1b';
    }
    else
    {
        return char_read;
    }
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
    BufferAdder(buffer, "\x1b[H", 3);                                // Uses the H command to reposition the cursor. It takes two arguments which are implicitly 1 and 1 ( row and column ).
                                                                  // So \x1b[H can also be written as \x1b[1;1H as here the rows and columns start from 1 not 0.
}

void hideCursor(struct appendBuffer* buffer)
{
    BufferAdder(buffer, "\x1b[?25l", 6);                          // This hides the cursor. For more: https://vt100.net/docs/vt100-ug/chapter3.html#SM
}                                                                 // https://vt100.net/docs/vt510-rm/DECTCEM.html

void showCursor(struct appendBuffer* buffer)
{
    BufferAdder(buffer, "\x1b[?25h", 6);                          // This hides the cursor. For more: https://vt100.net/docs/vt100-ug/chapter3.html#RM
}                                                                 // https://vt100.net/docs/vt510-rm/DECTCEM.html

void reinitializeCursor(struct appendBuffer* buffer)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", configuration.cursorY + 1, configuration.cursorX + 1); 
    // We changed the old H command into an H command with arguments, specifying the exact position we want the cursor to move to.
    BufferAdder(buffer, buf, strlen(buf));
}

void editorDrawRows(struct appendBuffer* buffer)
{
    for( int i = 0; i < configuration.screenrows; i ++ )
    {
        if( i == configuration.screenrows / 3 )
        {
            char welcomeMessage[80];
            int welcomeLength = snprintf(welcomeMessage, sizeof(welcomeMessage), "Leaf editor -- version %s", LEAF_VERSION); // this moves into welcomeMessage the given string where %s is replaced with over predefined macro the size of welcomeMessage
            if( welcomeLength > configuration.screencols )
                welcomeLength = configuration.screencols;
            
            int padding = (configuration.screencols - welcomeLength ) / 2;
            if( padding )
            {
                BufferAdder(buffer, "~", 1);
                padding --;
            }
            while(padding--)
            {
                BufferAdder(buffer, " ", 1);
            }
            BufferAdder(buffer, welcomeMessage, welcomeLength);
        }
        else
        {
            BufferAdder(buffer, "~", 1);
        }
        BufferAdder(buffer, "\x1b[K", 3);                         // this is used instead of the editorClearScreen function. This escape sequence clears the whole line. For more: https://vt100.net/docs/vt100-ug/chapter3.html#EL 
        if( i < configuration.screenrows - 1 )
            BufferAdder(buffer, "\r\n", 2);
    }
}

void refreshScreen()
{
    struct appendBuffer buffer = aBuf_init;

    hideCursor(&buffer);
    //editorClearScreen(&buffer);
    repositionCursor(&buffer);
    editorDrawRows(&buffer);
    reinitializeCursor(&buffer);
    showCursor(&buffer);

    write(STDOUT_FILENO, buffer.seq, buffer.len);
    BufferFree(&buffer);
}

/*** input ***/


void moveCursor(int key)
{
    switch(key)
    {
        case ARROW_LEFT:
            if( configuration.cursorX != 0 )
                configuration.cursorX --;
            break;
        case ARROW_RIGHT:
            if( configuration.cursorX != configuration.screencols - 1 )
                configuration.cursorX ++;
            break;
        case ARROW_UP:
            if( configuration.cursorY != 0 )
                configuration.cursorY --;
            break;
        case ARROW_DOWN:
            if( configuration.cursorY != configuration.screenrows - 1 )
                configuration.cursorY ++;
            break;
    }
}

void editorProcessKeypress()
{
    //this function processes the key pressed by the 
    //it maps keys combination to various editor functions 
    int char_read = editorReadKey();

    switch(char_read)
    {
        case CTRL_KEY('x'):
            write(STDOUT_FILENO, "\x1b[2J", 4);                    // Again clear the screen
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case PAGE_DOWN:
        case PAGE_UP:
            {
                int rows = configuration.screenrows;
                while( rows -- )
                {
                    moveCursor( char_read == PAGE_UP ? ARROW_UP : ARROW_DOWN );
                }
            }
            break;
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
            moveCursor(char_read);
            break;
    }
}

/*** init ***/

void initEditor()
{
    configuration.cursorX = 0;
    configuration.cursorY = 0;
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
