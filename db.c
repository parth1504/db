
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

typedef struct
{
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;

typedef enum{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum{ PREPARE_SUCCESS,PREPARE_SYNTAX_ERROR,PREPARE_NEGATIVE_ID,PREPARE_STRING_TOO_LONG, PREPARE_UNRECOGNIZED_STATEMENT} PreparedResult;

typedef enum{ STATEMENT_INSERT, STATEMENT_SELECT} StatementType;

typedef struct{
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE+1];
    char email[COLUMN_EMAIL_SIZE+1];
}Row;

typedef struct{
    StatementType type;
    Row row_to_insert;
}Statement;

#define size_of_attribute(Struct, Attribute) sizeof(((Struct*)0)->Attribute)
const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET =ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;


void serialize_row(Row* source, void* destination){
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username),USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE );
}

void deserialize_row(void* source, Row* destination){
    memcpy(&(destination->id), source+ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source+USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source+EMAIL_OFFSET, EMAIL_SIZE);
}
void print_row(Row* row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}


const uint32_t PAGE_SIZE=4096;
#define TABLE_MAX_PAGES 100
const uint32_t ROWS_PER_PAGE= PAGE_SIZE/ROW_SIZE;
const uint32_t TABLE_MAX_ROWS= TABLE_MAX_PAGES*ROWS_PER_PAGE;

typedef struct{
    uint32_t num_rows;
    void* pages[TABLE_MAX_PAGES];
}
Table;

void* row_slot(Table* table, uint32_t row_num){
    uint32_t page_num = row_num/ROWS_PER_PAGE;
    void* page = table->pages[page_num];
    if(page==NULL){
        page=table->pages[page_num]=malloc(PAGE_SIZE);
    }
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
}

Table* new_table(){
    Table* table= (Table*)malloc(sizeof(Table));
    table->num_rows=0;
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
    {
        table->pages[i]=NULL;
    }
    return table;
}

void free_table(Table* table){
    for(int i=0; table->pages[i];i++){
        free(table->pages[i]);
    }
    free(table);
}


InputBuffer* new_input_buffer(){
    InputBuffer* input_buffer= (InputBuffer*)malloc(sizeof(InputBuffer));
    input_buffer->buffer=NULL;
    input_buffer->buffer_length=0;
    input_buffer->input_length=0;
    return input_buffer;
}


void print_prompt(){ printf("db> ");}

//ssize_t getline(char **lineptr, size_t *n, FILE *stream);

void read_input(InputBuffer* input_buffer) {
    fflush(stdout); // Flush the output buffer to ensure the prompt is displayed
    size_t initial_buffer_size = 1024; // Set an initial buffer size
    input_buffer->buffer = (char*)malloc(initial_buffer_size);
    if (input_buffer->buffer == NULL) {
        printf("Memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->buffer_length = initial_buffer_size;

    if (fgets(input_buffer->buffer, input_buffer->buffer_length, stdin) == NULL) {
        free(input_buffer->buffer);
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length = strlen(input_buffer->buffer);

    // Check if the input length exceeds the buffer size, and reallocate if necessary
    while (input_buffer->input_length >= input_buffer->buffer_length - 1) {
        size_t new_buffer_size = input_buffer->buffer_length * 2;
        char* new_buffer = (char*)realloc(input_buffer->buffer, new_buffer_size);
        if (new_buffer == NULL) {
            free(input_buffer->buffer);
            printf("Memory reallocation error\n");
            exit(EXIT_FAILURE);
        }
        input_buffer->buffer = new_buffer;
        input_buffer->buffer_length = new_buffer_size;

        if (fgets(input_buffer->buffer + input_buffer->input_length, 
                   input_buffer->buffer_length - input_buffer->input_length, stdin) == NULL) {
            free(input_buffer->buffer);
            printf("Error reading input\n");
            exit(EXIT_FAILURE);
        }

        input_buffer->input_length += strlen(input_buffer->buffer + input_buffer->input_length);
    }
}

// void read_input(InputBuffer* input_buffer){
//     ssize_t bytes_read= getline(&(input_buffer->buffer),&(input_buffer->buffer_length), stdin);

//     if(bytes_read<=0){
//         printf("Error reading input\n");
//         exit(EXIT_FAILURE);
//     }
//     input_buffer->input_length=bytes_read-1;
//     input_buffer->buffer[bytes_read-1]=0;
// }

void close_input_buffer(InputBuffer* input_buffer){
    free(input_buffer->buffer);
    free(input_buffer);
}

MetaCommandResult do_meta_command(InputBuffer* input_buffer){
    if(strncmp(input_buffer->buffer,".exit",5)==0){
      //  printf("in meta command: %s", input_buffer->buffer);
        exit(EXIT_SUCCESS);
    }
    else{
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

PreparedResult prepare_insert(InputBuffer* input_buffer, Statement* statement){
    statement->type= STATEMENT_INSERT;
    char* keyword= strtok(input_buffer->buffer," ");
    char* id_string = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, "\n");
    if(id_string==NULL || username==NULL || email==NULL){
        return PREPARE_SYNTAX_ERROR;
    }
    int id=atoi(id_string);
    if (id < 0) {
       return PREPARE_NEGATIVE_ID;
    }
    if(strlen(username)>COLUMN_USERNAME_SIZE){
        return PREPARE_STRING_TOO_LONG;
    }
    if (strlen(email) > COLUMN_EMAIL_SIZE) {
       return PREPARE_STRING_TOO_LONG;
    }
    statement->row_to_insert.id=id;
    strcpy(statement->row_to_insert.username,username);
    strcpy(statement->row_to_insert.email,email);
    return PREPARE_SUCCESS;
}

PreparedResult prepare_statement(InputBuffer* input_buffer, Statement* statement){
    if(strncmp(input_buffer->buffer,"insert",6)==0){
        
        return prepare_insert(input_buffer,statement);
    }
    
    if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        if (input_buffer->buffer[6] == '\n' || input_buffer->buffer[6] == ' ' || input_buffer->buffer[6] == '\r') {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
        }
    }
    printf("%s", input_buffer->buffer);

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult execute_insert(Statement* statement, Table* table){
   
    if(table->num_rows>=TABLE_MAX_ROWS){
        return EXECUTE_TABLE_FULL;
    }
    Row* row_to_insert= &(statement->row_to_insert);
    serialize_row(row_to_insert, row_slot(table,table->num_rows));
    table->num_rows+=1;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement* statement, Table* table){
    Row row;
    for(uint32_t i=0;i<table->num_rows;i++){
        deserialize_row(row_slot(table, i), &row);
          
        print_row(&row);
    }
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement* statement, Table* table){
    switch(statement->type){
        case(STATEMENT_INSERT):
            return execute_insert(statement,table);
        case(STATEMENT_SELECT):
            return execute_select(statement,table);
    }
}

int main(int argc, char* argv[]){
    Table* table = new_table();
    InputBuffer* input_buffer= new_input_buffer();
    while (true)
    {
        print_prompt();
        read_input(input_buffer);
       // printf("In main: %s", input_buffer->buffer);
       if (input_buffer->buffer[0] == '.') { // Check for meta-commands
            switch (do_meta_command(input_buffer)) {
                case META_COMMAND_SUCCESS:
                    free_table(table);
                    close_input_buffer(input_buffer);
                    exit(EXIT_SUCCESS);
                case META_COMMAND_UNRECOGNIZED_COMMAND:
                    printf("Unrecognized command '%s'\n", input_buffer->buffer);
                    continue;
            }}
        
        // if(strcmp(input_buffer->buffer, ".exit")==0){
        //    if (input_buffer->buffer[0]=='.'){
        //         switch(do_meta_command(input_buffer)){
        //             case(META_COMMAND_SUCCESS):
        //                 continue;
        //             case(META_COMMAND_UNRECOGNIZED_COMMAND):
        //                 printf("Unrecognized command '%s'\n", input_buffer->buffer);
        //                 continue;
        //         }
        //    }

           
        // }

        Statement statement;
        switch(prepare_statement(input_buffer, &statement)){
            case(PREPARE_SUCCESS):
                break;
            case (PREPARE_NEGATIVE_ID):
                printf("ID must be positive.\n");
                continue;
            case (PREPARE_STRING_TOO_LONG):
                printf("String is too long.\n");
                continue;
            case (PREPARE_UNRECOGNIZED_STATEMENT):
                printf("Unrecognized keyword at start of '%s'.\n",
                input_buffer->buffer);
                continue;
            case(PREPARE_SYNTAX_ERROR):
                printf("Syntax error. Could not parse statement.\n");
            	continue;
        }
        switch (execute_statement(&statement, table)) {
	        case (EXECUTE_SUCCESS):
	             printf("Executed.\n");
	             break;
	        case (EXECUTE_TABLE_FULL):
	            printf("Error: Table full.\n");
	            break;
        }
    }
    
}

