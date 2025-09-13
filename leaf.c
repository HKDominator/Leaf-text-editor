/*** include ***/

#define _DEFAULT_SOURCE                                        // These three are needed for the getline function
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)                                // The CTRL_KEY macro bitwise-ANDs a character with the value 00011111
                                                                // It mirrors what the ctrl key does in the terminal: it strips bits 5 and 6 from whatever key you pressed in the combination with ctrl
#define LEAF_VERSION "0.0.4"
#define LEAF_TAB_STOP 8
#define LEAF_QUIT_TIMES 2
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

enum editorKey{
    BACKSPACE = 127,    
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

enum editorHighlight {
    HL_NORMAL = 0,
    HL_COMMENT,
    HL_MLCOMMENT,
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_STRING,
    HL_NUMBER,                       // each character which is a digit should have in the highlight array the HL_NUMBER value
    HL_MATCH
};

/*** data ***/

struct syntax{
    char* filetype; // name of the file type which will be desplayed
    char** filematch; // array of strings where each string contains a pattern to match a filename again. 
    char** keywords; // an array of keywords, being NULL terminated
    char*  singleline_comment_start; // where does an online comment starts
    char* multiline_comment_start; // where does a multiline comment start ( in c is /*)
    char* multiline_comment_end; // in c is */
    int flags; // flags is a bit field that will contain flags for whether to highlight numbers and whether to highlight strings for that filetype
};
typedef struct textRow{
    int size;
    int idx; // each row knows its index in the whole file
    int in_multiline_open_comment; // boolean flag
    int rsize; // the render size used for tabs or other non printable characters
    char* chars;
    char* render;
    unsigned char* highlight;  // each value from this array will correspond to a character in render
}textRow;

struct editorConfig{
    int screenrows, screencols;
    int row_offset; // keeps track of what rows are currently being shown
    int column_offset; // keeps track of what columns are currently being show
    int cursorX, cursorY;
    int renderX;    // the position of the cursor taking the tabs into consideration
    int rows_number;
    int dirty;      // this is a flag which tells whether the file has unsaved modifications
    textRow* row;
    char* filename;
    char statusmsg[80]; // these two are for the status message 
    time_t statusmsg_time;
    struct termios original_termios;                            // Original terminal state
    struct syntax* syntax;
}configuration;

/*** Filetypes ***/

char* C_HL_extensions[] = {".c", ".h", ".cpp", NULL};
char* C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL // the second type of keyword is differentaite using the | character
};

struct syntax HLDB[] = {    // HLDB = highlight database 
    {/*
        The syntax struct for the C language contains the string "c" for the filetype field, 
        the extensions ".c", ".h", and ".cpp" for the filematch field (the array must be terminated with NULL), 
        and the HL_HIGHLIGHT_NUMBERS flag turned on in the flags field.*/
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HDLB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** Prototypes ***/

void setStatusMessage(const char* format, ... ); // otherwise we wouldn't be able to compile the save to file function because we used there a function before it was defined
void refreshScreen();
char* prompt( char* prompt, void (*callback)(char* , int) );

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
                        case '1': return HOME_KEY; // The home key escape sequence is <esc>[1~ or <esc>[7~
                        case '3': return DEL_KEY; // The delete escape sequence is <esc>[3~
                        case '4': return END_KEY; // The end key escape sequence is <esc>[4~, <esc>[8~
                        case '5': return PAGE_UP; //The page up escape sequence is <esc>[5~
                        case '6': return PAGE_DOWN; // The page down escape sequence is <esc>[6~
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
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
                    case 'H': return HOME_KEY; //The home key escape sequence is <esc>[H
                    case 'F': return END_KEY; //The end key escape sequence is <esc>[F
                }
            }
        }
        else if( seq[0] == 'O')
        {
            switch(seq[1])
            {
                case 'H': return HOME_KEY;//The home key escape sequence is <esc>OH
                case 'F': return END_KEY;//The end key escape sequence is <esc>OF
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

/*** Syntax highlight ***/

int is_separator(int c)
{
    return isspace(c) || c == '\0' || strchr(",._(){}[]/+-=;*<>%", c) != NULL; // we use this function to properly delimit a number from a name which contains digits
}

void updateSyntax(textRow* row)
{
    row->highlight = realloc(row->highlight, row->rsize );
    memset(row->highlight, HL_NORMAL, row->rsize);

    if( configuration.syntax == NULL )
        return;

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = ( row->idx > 0 && configuration.row[row->idx - 1].in_multiline_open_comment); // used only for multi line comments

    char* comment_start = configuration.syntax->singleline_comment_start; // alias
    char* mcs = configuration.syntax->multiline_comment_start; // alias
    char* mce = configuration.syntax->multiline_comment_end; // alias

    int comment_start_len = comment_start ? strlen(comment_start) : 0;
    int mcs_len = mcs ? strlen(mcs) : 0;
    int mce_len = mce ? strlen(mce) : 0;

    char** keywords = configuration.syntax->keywords;   // just an alias

    int i = 0;
    while( i < row->rsize )
    {
        char c = row->render[i];
        unsigned char prev_hl = ( i > 0 ) ? row->highlight[i-1] : HL_NORMAL;

        if( comment_start_len && !in_string && !in_comment ) // single line comments should not be recognisd inside multi line comments
        {
            if( !strncmp(&row->render[i], comment_start, comment_start_len) )
            {
                memset(&row->highlight[i], HL_COMMENT, row->rsize - i);
                break;
            }
        }

        if( mcs_len && mce_len && !in_string ) // we need to have both mcs and mce different fro NULL to be in a multi line comment and also not to be in a string
        {
            if( in_comment )
            {
                row->highlight[i] = HL_MLCOMMENT;
                if( !strncmp( &row->render[i], mce, mce_len ) ) // we check if we are at the end of the multiline comment
                {
                    memset(&row->highlight[i], HL_MLCOMMENT, mce_len); // we set the whole comment terminator to the color 
                    i += mce_len; // and i consume it
                    in_comment = 0;
                    prev_sep = 1;
                    continue;
                }
                else
                {
                    i++;
                    continue;
                }
            }
            else if( !strncmp( &row->render[i], mcs, mcs_len ) ) // we check if we are at the beginning of the multi line comment
            {
                memset(&row->highlight[i], HL_MLCOMMENT, mcs_len);
                i += mcs_len;
                in_comment = 1;
                continue;
            }
        }

        if( configuration.syntax->flags & HL_HIGHLIGHT_STRINGS )
        {
            /*
             If in_string is set, then i know the current character can be highlighted with HL_STRING. 
             Then i check if the current character is the closing quote (c == in_string), and if so, we reset in_string to 0. 
             Then, since i highlighted the current character, i have to consume it by incrementing i and continueing out of the 
             current loop iteration. We also set prev_sep to 1 so that if i'm done highlighting the string, the closing quote 
             is considered a separator.

            If i'm not currently in a string, then i have to check if i'm at the beginning of one by checking 
            for a double- or single-quote. If i am, i store the quote in in_string, highlight it with HL_STRING, 
            and consume it.
            */
            if( in_string )
            {
                row->highlight[i] = HL_STRING;
                if( c == '\\' && i + 1 < row->size )
                {
                    row->highlight[i+1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if( c == in_string )
                    in_string = 0;
                i++;
                prev_sep = 1;
                continue;
            }
            else
            {
                if( c == '"' || c == '\'' )
                {
                    in_string = c;
                    row->highlight[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if( configuration.syntax->flags & HL_HIGHLIGHT_NUMBERS )
        {
            if(( isdigit(c) && ( prev_sep || prev_hl == HL_NUMBER )) ||
            (c == '.' && prev_hl == HL_NUMBER)) // supports numbers with decimal point
            {
                row->highlight[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if( prev_sep )
        {
            int j;
            for( j = 0; keywords[j]; j ++ )
            {
                int key_len = strlen(keywords[j]);
                int key2 = keywords[j][key_len - 1] == '|';
                if( key2 )  // checking the type of keyword
                    key_len --;
                if( !strncmp(&row->render[i], keywords[j], key_len) && 
                is_separator(row->render[i + key_len])) // a keyword needs a separator both before and after ( to eliminate this case: void, avoid)
                {
                    memset(&row->highlight[i], key2 ? HL_KEYWORD1 : HL_KEYWORD2, key_len);
                    i += key_len;
                    break;
                }
            }
            if( keywords[j] != NULL )
            {
                prev_sep = 0;
                continue;
            }
        }
        prev_sep = is_separator(c);
        i++;
    }
    int changed = (row->in_multiline_open_comment != in_comment );
    row->in_multiline_open_comment = in_comment; // tells me whether the ended as an unclosed multi-line comment or not. 
    if( changed && row->idx + 1 < configuration.rows_number )
    {
        /*
        Then i have to consider updating the syntax of the next lines in the file. So far, i have only been updating the 
        syntax of a line when the user changes that specific line. But with multi-line comments, a user could comment out an
        entire file just by changing one line. So it seems like i need to update the syntax of all the lines following the 
        current line. However, i know the highlighting of the next line will not change if the value of this line’s 
        in_multiline_open_comment did not change. So i check if it changed, and only call updateSyntax() on the next line 
        if in_multiline_open_comment changed (and if there is a next line in the file). Because updateSyntax() keeps calling 
        itself with the next line, the change will continue to propagate to more and more lines until one of them is unchanged,
        at which point i know that all the lines after that one must be unchanged as well.
        */
        updateSyntax(&configuration.row[row->idx + 1]);
    }
}

int syntaxToColor( int hl )
{
    switch( hl )
    {
        case HL_STRING: return 33; // strings -> yellow
        case HL_KEYWORD1: return 32; // one type of keyword will be green
        case HL_KEYWORD2: return 31; // the other type will be red
        case HL_NUMBER: return 35;   // digits -> magenta
        case HL_MATCH: return 96;   // matched characters -> bright cyan
        case HL_MLCOMMENT:  // we have the multi line comment the same color as the one liners
        case HL_COMMENT: return 90; // comments -> bright black
        default: return 37; // anything else -> normal
    }
}

void selectSyntaxHighlight()
{
    configuration.syntax = NULL;
    if( configuration.filename == NULL )
        return;
    char* extension = strrchr(configuration.filename, '.');

    for( unsigned int i = 0; i < HDLB_ENTRIES; i ++ )
    {
        struct syntax* syntax = &HLDB[i];
        int j = 0;
        while( syntax->filematch[j] )
        {
            /*
            I loop through each syntax struct in the HLDB array, and for each one of those, i loop through each pattern 
            in its filematch array. If the pattern starts with a ., then it’s a file extension pattern, and we use strcmp() 
            to see if the filename ends with that extension. If it’s not a file extension pattern, then i just check to 
            see if the pattern exists anywhere in the filename, using strstr(). If the filename matched according to those 
            rules, then i set configuration.syntax to the current syntax struct, and return.
            */
            int is_extension = ( syntax->filematch[j][0] == '.' );
            if(( is_extension && extension && !strcmp(extension, syntax->filematch[j] ))
            || (!is_extension && strcmp(configuration.filename, syntax->filematch[j])))
            {
                configuration.syntax = syntax;
                int filerow;
                for( filerow = 0; filerow < configuration.rows_number; filerow ++ )
                {
                    updateSyntax(&configuration.row[filerow]);  // to highlight when saving a new file with a specific extension
                }
                return;
            }
            j ++;
        }
    }
}

/*** Row operations ***/

int CursorXToRenderXConverter(textRow* row, int cursorX)
{
    /*I use rx % KILO_TAB_STOP to find out how many columns we are to the right of the last tab stop, 
    and then subtract that from KILO_TAB_STOP - 1 to find out how many columns we are to the left of the next tab stop. 
    We add that amount to rx to get just to the left of the next tab stop, and then the unconditional rx++ statement gets 
    us right on the next tab stop*/
    int renderX = 0;
    for( int i = 0; i < cursorX; i ++ )
    {
        if( row->chars[i] == '\t' )
        {
            renderX += (LEAF_TAB_STOP - 1) - (renderX % LEAF_TAB_STOP);
        }
        renderX ++;
    }
    return renderX;
}

int RederXToCursorXConverter( textRow* row, int renderX )
{
    /*
    I loop through the chars string, calculating the current rx value (current_renderX) as we go. But instead of stopping when I hit a particular 
    cursorX value and returning current_renderX, i want to stop when current_renderX hits the given renderX value and return cursorX.

    The return statement at the very end is just in case the caller provided an renderX that’s out of range, which shouldn’t happen. 
    The return statement inside the for loop should handle all renderX values that are valid indexes into render.
    */
    int current_renderX = 0;
    int cursorX;
    for( cursorX = 0; cursorX < row->size; cursorX ++ )
    {
        if( row->chars[cursorX] == '\t' )
        {
            current_renderX += ( LEAF_TAB_STOP - 1 ) - ( current_renderX % LEAF_TAB_STOP );
        }
        current_renderX ++;
        
        if( current_renderX > renderX ) return cursorX;
    }
    return cursorX;
}

void UpdateRow(textRow* row)
{
    int tabs = 0;
    for( int i = 0; i < row->size; i ++ )
        if( row->chars[i] == '\t' )
            tabs++;

    free(row->render);
    row->render = malloc( row->size + tabs*(LEAF_TAB_STOP - 1) + 1);
    int idx = 0;
    for( int i = 0; i < row->size; i ++ )
        if( row->chars[i] == '\t' )
        {
            row->render[idx++] = ' ';
            while( idx % LEAF_TAB_STOP != 0 ) row->render[idx++] = ' ';
        }
        else
        {
            row->render[idx++] = row->chars[i];
        }
    row->render[idx] = '\0';
    row->rsize = idx;
    updateSyntax(row);
}

void insertRow(int at, char* s, size_t len)             
{
    /*This funtion allocates a new text row in the text matrix and inserts it at the given position.*/
    if( at < 0 || at > configuration.rows_number )
        return;
    configuration.row = realloc(configuration.row, sizeof(textRow) * ( configuration.rows_number + 1 ) );
    memmove(&configuration.row[at + 1], &configuration.row[at], sizeof(textRow) * (configuration.rows_number - at));
    for( int i = at + 1; i <= configuration.rows_number; i ++ )
    {
        configuration.row[i].idx ++;
    }

    configuration.row[at].idx = at;
    configuration.row[at].size = len;
    configuration.row[at].chars = malloc(len + 1);
    memcpy(configuration.row[at].chars, s, len);
    configuration.row[at].chars[len] = '\0';

    configuration.row[at].rsize = 0;                                // initializing the render size and string for the new line
    configuration.row[at].render = NULL;

    configuration.row[at].highlight = NULL;
    configuration.row[at].in_multiline_open_comment = 0;
    UpdateRow(&configuration.row[at]);

    configuration.rows_number ++;
    configuration.dirty ++;
}   

void rowInsertChar( textRow* row, int at, int c )
{
    if( at < 0 || at > row->size )
        at = row->size;
    row->chars = realloc(row->chars, row->size + 2); // we allocate two more because one is for the new char and the second is for the terminator
    memmove(&row->chars[at + 1], &row->chars[at], row->size  - at + 1);//memmove() is used to copy a block of memory from a location to another. This first moves it into a buffer than into the new location so there is no problem with string overlap.
    row->size ++;
    row->chars[at] = c;
    UpdateRow(row);
    configuration.dirty ++;
}

void rowDeleteChar( textRow* row, int at)
{
    if( at < 0 || at > row->size )
        at = row->size;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size --;
    UpdateRow(row);
    configuration.dirty ++;
}

void freeRow(textRow* row)
{
    free(row->chars);
    free(row->render);
    free(row->highlight);
}

void deleteRow(int at)
{
    if( at < 0 || at >= configuration.rows_number ) //we validate the index
        return;
    freeRow(&configuration.row[at]);    //free memory owned by the deleted row
    memmove(&configuration.row[at], &configuration.row[at + 1], sizeof(textRow) * (configuration.rows_number - at - 1)); 
    for( int i = at; i < configuration.rows_number - 1; i ++ )
    {
        configuration.row[i].idx --;
    }
    configuration.rows_number --;//memmove() to overwrite the deleted row struct with the rest of the rows that come after it, and decrement the number of rows. 
    configuration.dirty ++;
}

void rowAppendString( textRow* row, char* s, size_t len )
{
    row->chars = realloc( row->chars, row->size + len + 1); // make space for the new string
    memcpy( &row->chars[row->size], s, len ); // copy the string at the end of the row
    row->size += len; // increase the size of the current line
    row->chars[row->size] = '\0'; // add the terminator
    UpdateRow(row);
    configuration.dirty ++;
}

/*** Editor operations ***/

void insertNewLine()
{
    if( configuration.cursorX == 0 )
    {
        insertRow(configuration.cursorY, "", 0);
    }
    else
    {
        textRow* row = &configuration.row[configuration.cursorY]; // we trunchiate the current row into two and we move  one on the next line,
        insertRow(configuration.cursorY + 1, &row->chars[configuration.cursorX], row->size - configuration.cursorX);
        row = &configuration.row[configuration.cursorY]; // we reinitialize our pointer and we place the terminator on our first part of the trunchiated sequence 
        row->size = configuration.cursorX;
        row->chars[row->size] = '\0';
        UpdateRow(row);
    }
    configuration.cursorY ++;
    configuration.cursorX = 0;
}

void insertChar(int c)
{
    if( configuration.cursorY == configuration.rows_number )
        insertRow(configuration.rows_number, "", 0);   // in case we are at the end of our file.
    rowInsertChar(&configuration.row[configuration.cursorY], configuration.cursorX, c);
    configuration.cursorX++;
}

void deleteChar()
{
    if( configuration.cursorY == configuration.rows_number )
        return;
    if( configuration.cursorX == 0 && configuration.cursorY == 0 )
        return;
    textRow* row = &configuration.row[configuration.cursorY];
    if( configuration.cursorX > 0 )
    {
        rowDeleteChar(row, configuration.cursorX - 1);
        configuration.cursorX --;
    }
    else
    {
        configuration.cursorX = configuration.row[configuration.cursorY - 1].size;
        rowAppendString(&configuration.row[configuration.cursorY - 1], row->chars, row->size);
        deleteRow(configuration.cursorY);
        configuration.cursorY --;
    }
}

/*** File I/O ***/

char* rowsToString( int* bufferLength )
{
    /*
    Joins all rows from the editor configuration into a single character buffer, separated by newline characters. 
    The function returns the buffer and stores its length in *bufferLength.
    The returned buffer:
     - Contains exactly the row data + one '\n' per row.
     - Is NOT null-terminated.
     - Must be freed by the caller after use.*/
    int totalLength = 0;
    for( int i = 0; i < configuration.rows_number; i ++ )
    {
        totalLength += configuration.row[i].size + 1;
    }
    *bufferLength = totalLength;

    char* buffer = malloc(totalLength);
    char* aux = buffer;
    for( int i = 0; i < configuration.rows_number; i ++ )
    {
        memcpy(aux, configuration.row[i].chars, configuration.row[i].size);//we copy in the auxiliary buffer the line
        aux += configuration.row[i].size; // we move the pointer with the size of the new sentence
        *aux = '\n';// we add the '\n' character where the auxiliary pointer points
        aux ++; // we move on and prepare for the next line
    }

    return buffer;
}

void editorOpen(char* filename)
{
    free(configuration.filename);
    configuration.filename = strdup(filename); //It makes a copy of the given string, allocating the required memory and assuming you will free() that memory.
    
    selectSyntaxHighlight();

    FILE* fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char* line = NULL;
    size_t capacity = 0;
    ssize_t length;

    while( (length = getline(&line, &capacity, fp)) != -1 ) //This goes to the file line by line and saves in my rows array each line
    {
    /*
    First, we pass it a null line pointer and a linecap (line capacity) of 0. That makes it allocate new memory for 
    the next line it reads, and set line to point to the memory, and set linecap to let you know how much memory it allocated. 
    Its return value is the length of the line it read, or -1 if it’s at the end of the file and there are no more lines 
    to read.
    */
        if( length != -1 )
        {
            while( length > 0 && ( line[length - 1] == '\n' || line[length - 1] == '\r' ) )
            {
                length --;
            }
            insertRow(configuration.rows_number, line, length);
        }
    }
    free(line);
    fclose(fp);
    configuration.dirty = 0; //to reset the dirty flag
}

void saveToFile()
{
    if( configuration.filename == NULL )
    {
        configuration.filename = prompt("Save as: %s (ESC to cancel)", NULL);
        if( configuration.filename == NULL )
        {
            setStatusMessage("Save aborted");
            return;
        }
        selectSyntaxHighlight();
    }
    int length;
    char* buffer = rowsToString(&length);

    int fd = open(configuration.filename, O_RDWR | O_CREAT, 0644 );  //flags needed by the open function
    if( fd != -1 )
    {
        if( ftruncate(fd, length) != -1 ) // this sets the file size with the given dimension
        {
            if( write(fd, buffer, length) == length ) // we added 3 layers of protection in case something fails.
            {
                close(fd);
                free(buffer);
                configuration.dirty = 0; // we reset the flag if we save the file
                setStatusMessage("%d bytes written to disk", length); // we send a mission acomplished message when we succesfully saved the file
                return;
            }
        }
        close(fd);
    }
    free(buffer);
    setStatusMessage("Saving failed. I/O error: %s", strerror(errno));
}

/*** find ***/

void findCallback(char* query, int key )
{
    /*
    In the callback, we check if the user pressed Enter or Escape, in which case they are leaving search mode so we return immediately instead of doing another search.
    
    The last feature i’d like to add is to allow the user to advance to the next or previous match in the file using the arrow keys. The ↑ and ← keys will go to the previous match, and the ↓ and → keys will go to the next match.

    I’ll implement this feature using two static variables in our callback. last_match will contain the index of the row that the last match was on, or -1 if there was no last match. And direction will store the direction of the search: 1 for searching forward, and -1 for searching backward.
    */

    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line; //saves the line of the last find
    static int* saved_hl = NULL; // dynamically allocated array which contains the highlight structre of the previously changed line

    if( saved_hl )
    {//if there is something to restore, we do it (we change back the color of the previously found sequence from blue to white)
        memcpy(configuration.row[saved_hl_line].highlight, saved_hl, configuration.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    if( key == '\r' || key == '\x1b' )
    {
        last_match = -1;
        direction = 1;
        return;
    }
    else if( key == ARROW_LEFT || key == ARROW_UP )
    {
        direction = -1;
    }
    else if(  key == ARROW_RIGHT || key == ARROW_DOWN )
    {
        direction = 1;
    }
    else
    {
        last_match = -1;
        direction = 1;
    }

    if( last_match == -1 )
        direction = 1;
    int current = last_match;

    for( int i = 0; i < configuration.rows_number; i ++ )
    {
        current += direction;
        if( current == -1 )
            current = configuration.rows_number - 1;
        else if( current == configuration.rows_number )
            current = 0;

        textRow* row = &configuration.row[current];
        char* match = strstr(row->render, query);
        if( match )
        {
            last_match = current;
            configuration.cursorY = current;
            configuration.cursorX = CursorXToRenderXConverter(row, match - row->render);
            configuration.row_offset = configuration.rows_number;

            saved_hl_line = current;
            saved_hl = malloc(row->rsize);//we load the things we will have to change
            memcpy(saved_hl, row->highlight, row->rsize);
            memset(&row->highlight[match - row->render], HL_MATCH, strlen(query));
            break;  
        }
    }
}

void find()
{
    int saved_cursorX = configuration.cursorX;
    int saved_cursorY = configuration.cursorY;
    int saved_coloffset = configuration.column_offset;
    int saved_rowoffset = configuration.row_offset;

    char* query = prompt("Search: %s (ESC or Enter to cancel | Arrows to navigate)", findCallback);
    if( query )
        free(query);
    else
    {
        configuration.cursorX = saved_cursorX;
        configuration.cursorY = saved_cursorY;
        configuration.row_offset = saved_rowoffset;
        configuration.column_offset = saved_coloffset;
    }
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

void drawStatusBar(struct appendBuffer* buffer)
{
    /* For more informations here is the link i used: https://vt100.net/docs/vt100-ug/chapter3.html#SGR
    Apparrently to invert the colours of a line there is an escape sequence: <esc>[7m and <esc>[m switches them back
    We will display in the status bar the following things:

    -the name of the file if a file is opened or [NO NAME] if we are not using a file
    -the number of lines in the file (again if that is the case)
    -the line number we are currently at
    
    */
    char status[80], lineNumber[80];
    BufferAdder(buffer, "\x1b[7m", 4);

    const char* dirty_msg = "";
    if( configuration.dirty > 0 )
    {
        if( configuration.dirty < 25 )
            dirty_msg = "(modified)";
        else if( configuration.dirty < 50 )
            dirty_msg = "(modified + )";
        else if( configuration.dirty < 250 )
            dirty_msg = "(heavily modified)";
        else if( configuration.dirty < 500 )
            dirty_msg = "(very dirty!)";
        else
            dirty_msg = "(EXTREMLY DIRTY!!!)";
    }
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
        configuration.filename ? configuration.filename : "[NO NAME]", configuration.rows_number, dirty_msg );//"prints" in the status char max 20 characters from the file name and the number of lines in the file
    int len_line_number = snprintf(lineNumber, sizeof(lineNumber), "%s | %d/%d", 
        configuration.syntax ? configuration.syntax->filetype : "no type", // say what filetype i have
        configuration.cursorY + 1, configuration.rows_number); // we use cursorY + 1 because it is 0 indexed
    
    if( len > configuration.screencols )
        len = configuration.screencols; //we make sure the status bar is only one line long
    BufferAdder(buffer, status, len);
    while( len < configuration.screencols )
    {
        if( configuration.screencols - len == len_line_number )
        {
            BufferAdder(buffer, lineNumber, len_line_number);
            break;
        }
        else
        {
            BufferAdder(buffer, " ", 1);
            len++;
        }
    }
    BufferAdder(buffer, "\x1b[m", 3);
    BufferAdder(buffer, "\r\n", 2);
}

void drawMessageBar(struct appendBuffer* buffer )
{
    BufferAdder(buffer, "\x1b[K", 3); //this clears the last row
    int msglen = strlen(configuration.statusmsg);
    if( msglen > configuration.screencols ) 
        msglen = configuration.screencols;
    if( msglen && time(NULL) - configuration.statusmsg_time < 5 )
        BufferAdder(buffer, configuration.statusmsg, msglen);
}

void editorScroll()
{
    configuration.renderX = 0;
    if( configuration.cursorY < configuration.rows_number )
        configuration.renderX = CursorXToRenderXConverter(&configuration.row[configuration.cursorY], configuration.cursorX);

    if( configuration.cursorY < configuration.row_offset )
    {
        configuration.row_offset = configuration.cursorY;
    }
    if( configuration.cursorY >= configuration.row_offset + configuration.screenrows)
    {
        configuration.row_offset = configuration.cursorY - configuration.screenrows + 1;
    }
    if( configuration.renderX < configuration.column_offset )
    {
        configuration.column_offset = configuration.renderX;
    }
    if( configuration.renderX >= configuration.column_offset + configuration.screencols )
    {
        configuration.column_offset = configuration.renderX - configuration.screencols + 1;
    }
}

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
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", ( configuration.cursorY - configuration.row_offset ) + 1, ( configuration.renderX - configuration.column_offset ) + 1); 
    // We changed the old H command into an H command with arguments, specifying the exact position we want the cursor to move to.
    BufferAdder(buffer, buf, strlen(buf));
}

void editorDrawRows(struct appendBuffer* buffer)
{
    for( int i = 0; i < configuration.screenrows; i ++ )
    {
        int file_row = i + configuration.row_offset;
        if( file_row >= configuration.rows_number )
        {
            if( configuration.rows_number == 0 && i == configuration.screenrows / 3 )
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
        }
        else
        {
            // char lineNumber[80];
            // int numberLength = snprintf(lineNumber, sizeof(numberLength), "%d ", configuration.row[file_row].idx - 1); // adds for each row at the beginning the row number
            // BufferAdder(buffer, lineNumber, numberLength);
            

            int len = configuration.row[file_row].rsize - configuration.column_offset;
            if( len < 0 ) 
                len = 0;
            if( len > configuration.screencols )
                len = configuration.screencols;

            char* c = &configuration.row[file_row].render[configuration.column_offset];
            unsigned char* hl = &configuration.row[file_row].highlight[configuration.column_offset];
            int currentColor = -1; //is used to not have to "feed" the buffer escape sequences after each character, only when a certain color changed
            
            for( int j = 0; j < len; j ++ )
            {
                /*
                    I can no longer just feed the substring of render that I want to print right into BufferAdder(). I’ll have to do it 
                    character-by-character from now on. If it is a digit character, we precede it with the <esc>[31m escape sequence and follow it by the 
                    <esc>[39m sequence.

                    I previously used the m command (Select Graphic Rendition) to draw the status bar using inverted colors. Now I am using it 
                    to set the text color. for more color related info check: https://en.wikipedia.org/wiki/ANSI_escape_code

                    The first table says that the text color can be set using codes 30 to 37, and reset it to the default color using 39. 
                    The color table says 0 is black, 1 is red, and so on, up to 7 which is white. 
                    I can set the text color to magenta using 35 as an argument to the m command. After printing the digit, 
                    I use 39 as an argument to m to set the text color back to normal.
                    */
                if( iscntrl( c[j] ) )
                {
                    /*
                    I'm going to translate nonprintable characters into printable ones. I’ll render the alphabetic control 
                    characters (Ctrl-A = 1, Ctrl-B = 2, …, Ctrl-Z = 26) as the capital letters A through Z. I’ll also render 
                    the 0 byte like a control character. Ctrl-@ = 0, so i’ll render it as an @ sign. Finally, any other 
                    nonprintable characters i’ll render as a question mark (?). And to differentiate these characters from
                     their printable counterparts, i’ll render them using inverted colors (black on white).
                    */
                    char sym = (c[j] <= 26 ) ? '@' + c[j] : '?';
                    BufferAdder(buffer, "\x1b[7m", 4);
                    BufferAdder(buffer, &sym, 4);
                    BufferAdder(buffer, "\x1b[m", 3);
                    if( currentColor != -1 )
                    {
                        char buf[16]; //inverted colors
                        int char_len = snprintf(buf, sizeof(buf), "\x1b[%dm", currentColor);
                        BufferAdder(buffer, buf, char_len);
                    }
                }
                else if( hl[j] == HL_NORMAL )
                {
                    if( currentColor != -1 )
                    {
                        BufferAdder(buffer, "\x1b[39m", 5);
                        currentColor = -1;
                    }
                    BufferAdder(buffer, &c[j], 1);
                }
                else
                {
                    int color = syntaxToColor(hl[j]);
                    if( color != currentColor )
                    {
                        currentColor = color;           
                        char buf[16];
                        int color_len = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        BufferAdder(buffer, buf, color_len);
                    }

                    BufferAdder(buffer, &c[j], 1);
                }
            }
            BufferAdder(buffer, "\x1b[39m", 5);
        }
        BufferAdder(buffer, "\x1b[K", 3);                         // this is used instead of the editorClearScreen function. This escape sequence clears the whole line. For more: https://vt100.net/docs/vt100-ug/chapter3.html#EL 
        BufferAdder(buffer, "\r\n", 2);
    }
}

void refreshScreen()
{
    struct appendBuffer buffer = aBuf_init;
    editorScroll();
    hideCursor(&buffer);
    //editorClearScreen(&buffer);
    repositionCursor(&buffer);
    editorDrawRows(&buffer);
    drawStatusBar(&buffer);
    drawMessageBar(&buffer);

    reinitializeCursor(&buffer);
    showCursor(&buffer);

    write(STDOUT_FILENO, buffer.seq, buffer.len);
    BufferFree(&buffer);
}

void setStatusMessage(const char* format, ... )
{
    va_list ap;                // comes from the stdarg library. lets you access the variable arguments (...).
    va_start(ap,format);       // va_start sets up ap so it points to the first unnamed argument (the ones in ...).
    vsnprintf(configuration.statusmsg, sizeof(configuration.statusmsg), format, ap); //vsnprintf is like snprintf, but it takes a va_list instead of a variable number of arguments.
    va_end(ap);                //This tells the system you’re done with the variable arguments.
    configuration.statusmsg_time = time(NULL);
}

/*** input ***/

char* prompt(char* prompt, void (*callback)(char*, int)) // it takes a callback function as an argument. I will call this function after each keypress, passing the current search query inputted by the user and the last key they pressed.
{
    size_t size = 128;
    char* buffer = malloc(size);

    size_t length = 0;
    buffer[0] = '\0';

    while(1)
    { //this is called when we want to save a file as someting. The infinite loop wait for 
        setStatusMessage(prompt, buffer);
        refreshScreen();

        int c = editorReadKey();
        if( c == '\r' )
        {
            if( length != 0 )
            {
                setStatusMessage("");
                if( callback )
                    callback(buffer, c);
                return buffer;
            }
        }
        else if( c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE )
        {
            if( length != 0 )
                buffer[--length] = '\0';
        }
        else if( c == '\x1b' )
        {
            setStatusMessage("");
            if( callback )
                callback(buffer, c);
            free(buffer);
            return NULL;
        }
        else if( !iscntrl(c) && c < 128 )
        {
            if( length == size - 1 )
            {
                size *= 2;
                buffer = realloc(buffer, size);
            }
            buffer[length++] = c;
            buffer[length] = '\0';
        }

        if( callback )
            callback(buffer, c);
    }
}

void moveCursor(int key)
{
    textRow* row = (configuration.cursorY >= configuration.rows_number ) ? NULL : &configuration.row[configuration.cursorY];

    switch(key)
    {
        case ARROW_LEFT:
            if( configuration.cursorX != 0 ) /// was 0 
                configuration.cursorX --;
            else if(configuration.cursorY > 0)
            {
                configuration.cursorY --;
                configuration.cursorX = configuration.row[configuration.cursorY].size;  // allows the user to press ← at the beginning of the line to move to the end of the previous line.
            }
            break;
        case ARROW_RIGHT:
            if( row && configuration.cursorX < row->size)
            {
                configuration.cursorX ++;
            }
            else if( row && configuration.cursorX == row->size )
            {
                configuration.cursorY ++;
                configuration.cursorX = 0; // 0
            }
            break;
        case ARROW_UP:
            if( configuration.cursorY != 0 )
                configuration.cursorY --;
            break;
        case ARROW_DOWN:
            if( configuration.cursorY < configuration.rows_number )
                configuration.cursorY ++;
            break;
    }
    row = (configuration.cursorY >= configuration.rows_number ) ? NULL : &configuration.row[configuration.cursorY];
    int row_length = row ? row->size : 0;                       // this section is used to correct the cursor positioning in case
    if( configuration.cursorX > row_length )                    // you go down from a long line to a short line. It snaps the curosr to the end
        configuration.cursorX = row_length;                     // of the line.
}

void editorProcessKeypress()
{
    //this function processes the key pressed by the 
    //it maps keys combination to various editor functions 
    int char_read = editorReadKey();
    static int quit_times = LEAF_QUIT_TIMES;


    switch(char_read)
    {
        case '\r':
            insertNewLine();
            break;
        case CTRL_KEY('x'):
            if ( configuration.dirty && quit_times > 0 )
            {
                setStatusMessage("WARNING! FIle has unsaved changes. "
                "Press Ctrl-X %d more times to quit.", quit_times);
                quit_times --;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);                    // Again clear the screen
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case CTRL_KEY('s'):
            saveToFile();
            break;
        case PAGE_DOWN:
        case PAGE_UP:
            {
                if( char_read == PAGE_UP ) //Now that i have scrolling, let’s make the Page Up and Page Down keys scroll up or down an entire page.
                {
                    configuration.cursorY = configuration.row_offset;
                }
                else if( char_read == PAGE_DOWN )
                {
                    configuration.cursorY = configuration.row_offset + configuration.screenrows - 1;
                    if( configuration.cursorY > configuration.rows_number )
                        configuration.cursorY = configuration.rows_number;
                }
                
                int rows = configuration.screenrows;
                while( rows -- )
                {
                    moveCursor( char_read == PAGE_UP ? ARROW_UP : ARROW_DOWN );
                }
            }
            break;
        case HOME_KEY:
            configuration.cursorX = 0;
            break;
        case END_KEY:
            if( configuration.cursorY < configuration.rows_number ) 
                configuration.cursorX = configuration.row[configuration.cursorY].size; //the end key press will make the cursor go to the end of the current line
            break;
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
            moveCursor(char_read);
            break;
        
        case CTRL_KEY('f'):
            find();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if( char_read == DEL_KEY ) moveCursor(ARROW_RIGHT);
            deleteChar();
            break;
        case CTRL_KEY('l'):
        case '\x1b':        //we don't do anything when these to are pressed. For example, ctrl-l key refreshes the terminal but the terminal is automatically refreshed
            break;

        default:
            insertChar(char_read); //in case there is no keypress mapped to something, then it will be interpreted as a simple character
            //and it will be added.
            break;
    }

    quit_times = LEAF_QUIT_TIMES;
}

/*** init ***/

void initEditor()
{
    configuration.cursorX = 0;
    configuration.cursorY = 0;
    configuration.rows_number = 0;
    configuration.column_offset = 0;
    configuration.row_offset = 0;
    configuration.renderX = 0;
    configuration.row = NULL;
    configuration.filename = NULL;
    configuration.statusmsg[0] = '\0';
    configuration.statusmsg_time = 0;
    configuration.dirty = 0;
    configuration.syntax = NULL;    // no filetype for the current file
    if( getWindowSize(&configuration.screenrows, &configuration.screencols) == -1 )
        die("getWidnowSize");
    configuration.screenrows -=2 ; // we leave an empty line at the end for the status bar and another one for the message box
}


int main(int argc, char* argv[] )
{
    enableRawMode();
    //we now want to be able to take input from the users keyboard
    initEditor();   
    if( argc >= 2 )
    {
        editorOpen(argv[1]);
    }

    setStatusMessage("HELP: Ctrl-X = quit | SAVE: Ctrl-S = save | FIND: Ctrl-F = find   ");

    while(1)                                            //we changed such that the terminal is not waiting for some input
    {
        refreshScreen();
        editorProcessKeypress();
    }        
    return 0;
}
