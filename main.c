#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <string.h>

static struct termios original_termios;
int tty_fd;

#define DEBUG_MODE 0

#define MAX_PATH_LEN 256 //make it more safe later
#define STARTING_TEXT_LINES 8
#define STARTING_LINE_LEN 32

#define FULL_SCREEN_REFRESH "\e[2J\e[H" // Defines escape code sequences that refreshes screen and moves cursor to terminal's default top-left position
#define REFRESH_ABOVE_STATUS_BAR "\e[2A\e[1J\e[H" // Moves cursor 2 lines up, refreshes from cursor to beginning of terminal default position (X: 1, Y: 1) (top-left of window) and places terminal's cursor there.
#define REFRESH_BELOW_CURSOR "\e[0J"
#define CURSOR_JUMP_1_LINE_UP "\e[1A"
#define CURSOR_MOVE_1_COL_LEFT "\e[1D"
#define REFRESH_ENTIRE_LINE "\e[2K"
//#define CURSOR_MOVE_BEG_OF_LINE "\e[E\e[F"
#define SAVE_CURSOR_POS "\e7"
#define RESTORE_CURSOR_POS "\e8"
//                           "---\r\nCTRL+S to save to the file | CTRL+O to open a file | CTRL+N to open new file | CTRL+Q to exit the program"
#define STATUS_BAR_TEXT_LONG "---\r\n\e[1m\e[3mCTRL+S\e[0m to save to the file | \e[1m\e[3mCTRL+O\e[0m to open a file | \e[1m\e[3mCTRL+N\e[0m to open new file | \e[1m\e[3mCTRL+Q\e[0m to exit the program"
//                      "---\r\nCTRL+S to save | CTRL+O to open file | CTRL+N to open new | CTRL+Q to exit"
#define STATUS_BAR_TEXT "---\r\n\e[1m\e[3mCTRL+S\e[0m to save | \e[1m\e[3mCTRL+O\e[0m to open file | \e[1m\e[3mCTRL+N\e[0m to open new | \e[1m\e[3mCTRL+Q\e[0m to exit"
//                            "---\r\nCTRL+S save | CTRL+O open | CTRL+N new | CTRL+Q exit"
#define STATUS_BAR_TEXT_SHORT "---\r\n\e[1m\e[3mCTRL+S\e[0m save | \e[1m\e[3mCTRL+O\e[0m open | \e[1m\e[3mCTRL+N\e[0m new | \e[1m\e[3mCTRL+Q\e[0m exit" // Prints help/"tutorial" info on the bottom status bar

int key_handling(char curr_path[], char c, char ***text_lines, int *line_number, int *char_number, int *line_count, int **char_count_in_lines, int lite_mode_flag);

int open_new_file_logic(char text[], int *count);
int open_file_logic(char *path, char text[], int* count);
int save_file_logic(char curr_path[], char text[], int* count);

int was_last_char_backsp = 0; // if last character was backspace move cursor 1 pixel to the left to make it feel more responsive
int file_opened_flag = 0;
int file_saved_flag = 0;

//TO DO:
//- text with dynamic memory allocation?
//- Valid text scrolling (status bar always present) (!!!!!)
//- Proper arrow keys handling and inserting text in the middle/beginning of the text (more work here) (!!!!!)
//  Probably first switch to array of lines and then to gap buffer or piece table

void disable_raw_mode() {
    if (tcsetattr(tty_fd, TCSAFLUSH, &original_termios) == -1) {
        perror("tcsetattr restore");
    }
}

void enable_raw_mode() {
        //We first remember it with tcgetattr() by writing to orginal_termios termios struct,
        //in order to restore the terminal back to its user's previous state (with use of disable_raw_mode() function).
    tty_fd = open("/dev/tty", O_RDWR);
    if (tty_fd == -1) {
        perror("open /dev/tty");
        exit(1);
    }
    
    if (tcgetattr(tty_fd, &original_termios) == -1) {
        perror("tcgetattr");
        exit(1);
    }

    atexit(disable_raw_mode);

    struct termios raw = original_termios;

    // Input flags (disabling)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Output flags (disabling)
    raw.c_oflag &= ~(OPOST);

    // Control flags (enabling)
    raw.c_cflag |= (CS8);

    // Local flags (disabling)
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // Control chars
    raw.c_cc[VMIN] = 1;   // read blocks until 1 byte
    raw.c_cc[VTIME] = 0;  // no timeout

    if (tcsetattr(tty_fd, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        exit(1);
    }
}

volatile sig_atomic_t window_resized = 0;

void handle_sigwinch(int signo) { // i.e: handle a SIGnal of WINdow CHange
    (void)signo;
    window_resized = 1;
}

int get_window_size(struct winsize *ws) {


    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, ws) == -1 || ws->ws_col == 0) {
        return -1; //error
    }

    return 0;
}

int get_cursor_position(int fd, int *row, int *col) {

    char buf[32];
    unsigned int i = 0;

    // Asking terminal: "where is cursor?"
    if (write(fd, "\e[6n", 4) != 4) {
        return -1;
    }

    // Read response: \e [ row ; col R
    while (i < sizeof(buf) - 1) {
        if (read(fd, &buf[i], 1) != 1) {
            break;
        }
        if (buf[i] == 'R') {
            break;
        }
        i++;
    }

    buf[i + 1] = '\0';

    // Expected format: \e [ rows ; cols R
    if (buf[0] != '\e' || buf[1] != '[') {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", row, col) != 2) {
        return -1;
    }

    return 0;
}

int open_new_file_logic(char text[], int *count){
    
    char temp_c;
    printf(FULL_SCREEN_REFRESH);
    printf("Are you sure you wanna open new file? This will delete all unsaved progress on current file [Y/n]");
    while(1){
        if(read(tty_fd, &temp_c, 1) == 1){
            if(temp_c == 'Y' || temp_c == 'y' || temp_c == '\r'){
                memset(text, 0, *count);
                *count = 0;
                break;
            }else if(temp_c == 'n'){
                break;
            }
        }
    }
}

int open_file_logic(char curr_path[], char text[], int* count){

    printf(FULL_SCREEN_REFRESH);
    printf("Please input the name or path and name of file you want to open (You can use relative or absolute path):\r\n\r\n");
    char c;
    int temp_count = 0;
    char new_path[MAX_PATH_LEN] = "";
    printf(SAVE_CURSOR_POS);
    while(1){
        if (read(tty_fd, &c, 1) == 1) {
            //key_handling(curr_path, c, new_path, &temp_count, 1);
            printf(REFRESH_ENTIRE_LINE);
            printf(RESTORE_CURSOR_POS);
            printf("%s", new_path); // printing currently written path name's text
            if(c == 13 /*CR*/){ // if enter (return) is pressed stop waiting for more characters and accept the given file name
                new_path[(*count)-1] = '\0'; //getting rid of CR character at the end of file name.
                break;
            }
        }
    }

    int fd = open(new_path, O_RDONLY);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    /*ssize_t bytes_read = read(fd, text, TEXT_MAX_LEN - 1);
    if (bytes_read == -1) {
        perror("read");
        close(fd);
        return -1;
    }

    text[bytes_read] = '\0';
    *count = (int)bytes_read;
    strcpy(curr_path, new_path);*/

    close(fd);
    file_opened_flag = 1;
    return 0;
}

int save_file_logic(char curr_path[], char text[], int* count){

    printf(FULL_SCREEN_REFRESH);
    if(strcmp(curr_path, "") == 0){
        printf("Please input the saved file name or path and name (You can use relative or absolute path):\r\n\r\n");
        char c;
        int temp_count = 0;
        printf(SAVE_CURSOR_POS);
        while(1){
            if (read(tty_fd, &c, 1) == 1) {
                //key_handling(curr_path, c, curr_path, &temp_count, 1);
                printf(REFRESH_ENTIRE_LINE);
                printf(RESTORE_CURSOR_POS);
                printf("%s", curr_path); // printing currently written path name's text
                if(c == 13 /*CR*/){ // if enter (return) is pressed stop waiting for more characters and accept the given file name
                    curr_path[(*count)-1] = '\0'; //getting rid of CR character at the end of file name.
                    break;
                }
            }
        }
    }

    int fd = open(curr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open");
        return -1;
    }

    ssize_t bytes_written = write(fd, text, *count);
    if (bytes_written == -1) {
        perror("write");
        close(fd);
        return -1;
    }

    if (bytes_written != *count) {
        fprintf(stderr, "Partial write!\n");
        close(fd);
        return -1;
    }


    close(fd);
    file_saved_flag = 1;
    return 0;
}


int file_logic(){

}

//                                                 lite mode is used for key handling in options like: setting file name to save it etc. text areas v 
int key_handling(char curr_path[], char c, char ***text_lines, int *line_number, int *char_number, int *line_count, int **char_count_in_lines, int lite_mode_flag){

    if(lite_mode_flag == 0){ //if lite mode isn't present handle all the special characters as well.

        if (c == 19) { // CTRL + S (saves to a file)

            //save_file_logic(curr_path, text, count);
            return 0;

        }else if (c == 15) { // CTRL + O (opens a file)

            //open_file_logic(curr_path, text, count);
            return 0;

        }else if (c == 17) { // CTRL + Q (quits program)

            return -1; // Quit program

        }else if (c == 14) { // CTRL + N (Opens new file)

            //open_new_file_logic(text, count);
            return 0;

        }else if (c == 13 /*CR*/) { // <- "return" handling

            // Adding (allocating more) lines to text_lines line array in case it's needed.
            if(*line_number >= (*line_count)-1){// Safety measurements
                char **extended_text_lines = realloc(*text_lines, (*line_count * 2) * sizeof(char*));
                int *extended_char_count_in_lines = realloc(*char_count_in_lines, (*line_count * 2) * sizeof(int));
                if (extended_text_lines == NULL){ // Safety measurements
                    perror("unable to perform realloc for extended_text_lines!");
                    exit(1);
                    // Maybe do some kind of emergency save later here?
                }else if (extended_char_count_in_lines == NULL){ // Safety measurements
                    perror("unable to perform realloc for extended_text_lines!");
                    exit(1);
                }else{

                    *text_lines = extended_text_lines; // New char** pointer appended with new few lines (multiplying current number of lines by 2)
                    *char_count_in_lines = extended_char_count_in_lines;
                    for(int i = *line_count; i < *line_count * 2; i++){
                        *(*text_lines+i) = calloc(STARTING_LINE_LEN, sizeof(char)); // Each line 128 characters by default.
                        *(*char_count_in_lines+i) = STARTING_LINE_LEN;
                    }
                        //!!!!! LATER REMEMBER LINES CAN HAVE DIFFERENT LENGTHS AND CHECK FOR THAT !!!!!
                    *line_count *= 2;
                }
            }

            *line_number += 1;
            *char_number = 0;

            (*text_lines)[*line_number][*char_number] = '\0';
            
            return 0;

        }

    }

    //                      v do better other lines handling later (?)
    if (c == 127 /*DEL*/ && *char_number >= 1) { // <- DEL ("backspace") handling

        (*text_lines)[*line_number][*char_number] = '\0'; //deleting the character that was before delete (there)
        *char_number -= 1;

        was_last_char_backsp = 1;

        return 0;

    }else if(c < 32){ // Skipping other undefined non-printable characters
        return 0;
    }

    (*text_lines)[*line_number][*char_number] = c;

    //We need to do both!!! Increase size (add more chars) and change the value for it
    //Remember to add entry while adding new lines as well!!!
    if(*char_number >= *(*char_count_in_lines + *line_number) - 1){// Safety measurements     or: *(*char_count_in_lines + *line_number) * 2
        char *extended_char_line = realloc(*(*text_lines + *line_number), (*(*char_count_in_lines + *line_number) * 2) * sizeof(char));
        //nullify the new chars
        if (extended_char_line == NULL){ // Safety measurements
            perror("unable to perform realloc!");
            exit(1);
            // Maybe do some kind of emergency save later here?
        }else{

            *(*text_lines + *line_number) = extended_char_line; // New char** pointer appended with new few lines (multiplying current number of lines by 2)
            for(int i = (*char_count_in_lines)[*line_number]; i < (*char_count_in_lines)[*line_number] * 2; i++)
                (*text_lines)[*line_number][i] = '\0'; // Each line 128 characters by default.
                //memncpy would be easier tbh (?) !!!
            (*char_count_in_lines)[*line_number] *= 2;
        }
    }
    *char_number += 1; // handling here too!!!
    (*text_lines)[*line_number][*char_number] = '\0';

    return 0;
}

int print_logic(struct winsize *ws, char curr_path[], char **text_lines, int line_count){ // The editor's main printing/render logic

    if(window_resized == 1){
        get_window_size(ws);
        if(DEBUG_MODE) printf("Window size has changed!\r\n");
        window_resized = 0;
    }

    if(DEBUG_MODE){}
    else{
        
        //Infos at the bottom of the screen (bottom status bar). (change to macro function later?)
        printf("\e[%d;1;H", ws->ws_row-1);
        if(file_saved_flag){

            printf("---\r\nFile \"%s\" saved successfully!", curr_path);
            file_saved_flag = 0;

        }else if(file_opened_flag){

            printf("---\r\nFile \"%s\" opened successfully!", curr_path);
            file_opened_flag = 0;
            
        }else{
            if(ws->ws_col >= strlen(STATUS_BAR_TEXT_LONG) - 12*4) //12 is the number of special characters used for formatting (invis) all the "CTRL+" commands
                printf(STATUS_BAR_TEXT_LONG);
            else if(ws->ws_col >= strlen(STATUS_BAR_TEXT) - 12*4)
                printf(STATUS_BAR_TEXT);
            else
                printf(STATUS_BAR_TEXT_SHORT);
        }
        printf(REFRESH_ABOVE_STATUS_BAR);

        //Printing proper editor's main text:
        for(int i = 0; i < line_count; i++)
            printf("%s\r\n", text_lines[i]);

        if(was_last_char_backsp){
            printf(CURSOR_MOVE_1_COL_LEFT);
            was_last_char_backsp = 0;
        }
    }
}

int main(void) {

    signal(SIGWINCH, handle_sigwinch); // Signal (works/is run asynchronously) in case terminal's window size changes.

    enable_raw_mode();

    printf(FULL_SCREEN_REFRESH); // Initial screen refresh

    int line_count = STARTING_TEXT_LINES;
    int curr_line_num = 0;
    int curr_char_num = 0; 

    //Change first init to malloc maybe (to optimize/save performance)?
    char **text_lines = calloc(line_count, sizeof(char*)); // Start with 128 lines
    int *char_count_in_lines = calloc(line_count, sizeof(int)); // dynamic table of characters amount in given lines
    // Think whether the data structure wouldn't be a better idea later
    for(int i = 0; i < line_count; i++){
        *(text_lines+i) = calloc(STARTING_LINE_LEN, sizeof(char)); // Each line 128 characters by default.
        *(char_count_in_lines+i) = STARTING_LINE_LEN;
    }

    int count = 0; // Current editor's text length
    char curr_path[MAX_PATH_LEN] = ""; // Current file path
    setbuf(stdout, NULL);
    struct winsize *ws = malloc(sizeof(struct winsize));
    get_window_size(ws);

    char c;

    while (1) {

        print_logic(ws, curr_path, text_lines, line_count);

        if (read(tty_fd, &c, 1) == 1) {

            if(DEBUG_MODE){

                printf("You pressed: %c %d\r\n", c, c);
                printf("Window size:\r\nrows: %d, cols: %d\r\n", ws->ws_row, ws->ws_col);
                if(c == 'q') break;
                printf("===================\r\n");

            }else{

                if(key_handling(curr_path, c, &text_lines, &curr_line_num, &curr_char_num, &line_count, &char_count_in_lines, 0) == -1) // Normally doesn't return -1, so if that's the case then:
                    break; //Exits the program
                
            }
        }
    }

    if(DEBUG_MODE){}
    else{
        printf(FULL_SCREEN_REFRESH);
        printf("Quitting the editor...\r\n");
    }

    // Freeing window size data structure
    free(ws);
    // Freeing text_lines container contents
    for(int i = 0; i < line_count; i++){
        free(*(text_lines+i));
    }
    free(text_lines);
    // Freeing char_count_in_lines contents
    free(char_count_in_lines);
    return 0;
}
