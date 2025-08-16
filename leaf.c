#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios original_termios;

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);      //when exiting, we reset the terminal to its original state
}

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &original_termios);                  // read the attributes in a struct 
    atexit(disableRawMode);                                      // register our disableRawMode() function to be called automatically when the program exits 
   
    struct termios to_raw = original_termios;                    // copy of the initial state of the terminal
    //The ECHO feature causes each key you type to be printed to the terminal, so you can see what you’re typing.
    to_raw.c_lflag &= ~( ECHO | ICANON );                        // change the attribute by hand + c_lglah = dumping ground for other state
    //ECHO=00000000000000000000000000001000 in binary. It is a bitflag
    //ICANON is not an input flag even though the majority of input flags start with I. It is a local flag in c_lflag
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &to_raw);                 // set back the attributes
    //With this part, we no longer see on the screen the keys we pressed
}


int main()
{
    enableRawMode();
    //we now want to be able to take input from the users keyboard
    char ant, pos;
    while( read(STDIN_FILENO, &ant, 1) == 1  )          //reads one byte (character) from the keyboard until there are
    {
        if( ant == '\n') continue;  
        if( ant == '!' && pos == 'x' )                  //you can exit by entering x!
            break;
        pos = ant;
    }        
					                                    //no more characters to be read
    return 0;
}
