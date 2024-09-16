#include <iostream>
#include <string>
#include <unistd.h> 
#include "parser.h" 
#include <sys/wait.h>
#include <type_traits>
#include <signal.h>
using namespace std;
template<typename T>
void pipeline_execution(const T input, int num);
void parallel_execution(parsed_input input, int num, int pipe_array[][2], int child_pids[]);
void sequential_execution(parsed_input input, int num);

void execute_single_command(command c){
    pid_t pid;
    int status;
    if((pid = fork()) == 0){
        execvp(c.args[0], c.args);
        exit(EXIT_FAILURE);
    }
    else{
        waitpid(pid, &status, 0);
    } 
}
void repeater(int pipe_array[][2], int num){
    if(!fork()){
        char line[INPUT_BUFFER_SIZE];
        int is_eof;
        while((is_eof = read(STDIN_FILENO, line, INPUT_BUFFER_SIZE)) > 0){
            for(int i = 0; i < num; ++i){
                write(pipe_array[i][1], line, is_eof);
            }
        }
        for(int i = 0; i < num; ++i){
            close(pipe_array[i][1]);
        }
        exit(EXIT_SUCCESS);
    }
    for(int i = 0; i < num; ++i){
        close(pipe_array[i][1]);
    }

}

void subshell_execution(const char * input){
    char subshell_input[INPUT_BUFFER_SIZE];
    strcpy(subshell_input, input);
    parsed_input input_parsed; 
    parse_line(subshell_input, &input_parsed);
    pid_t pid; int status;
    int num = input_parsed.num_inputs;
    pid_t child_pids[num];
    if(!(pid = fork())){
        if(input_parsed.num_inputs == 1 && input_parsed.inputs[0].type == INPUT_TYPE_COMMAND){
            execute_single_command(input_parsed.inputs[0].data.cmd);
        }
        else if(input_parsed.separator == SEPARATOR_SEQ){
            sequential_execution(input_parsed, input_parsed.num_inputs);
        }
        else if(input_parsed.separator == SEPARATOR_PIPE){
            pipeline_execution(input_parsed, input_parsed.num_inputs);
        }
        else if(input_parsed.separator == SEPARATOR_PARA){
            int pipe_array[num][2];
            parallel_execution(input_parsed, num, pipe_array,child_pids);
            repeater(pipe_array, num);
        }
        exit(EXIT_SUCCESS);
    } 
    else{
        waitpid(pid, &status, 0);
        for(int i = 0; i < num; ++i){
            int status;
            waitpid(child_pids[i], &status, 0);
        }
    }   

}

template<typename T>
void pipeline_execution(const T input, int num){
    int pipe_array[num - 1][2];
    for(int i = 0; i < num - 1; ++i){
        if(pipe(pipe_array[i]) < 0){
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }
    pid_t child_pids[num];
    for(int i = 0; i < num; ++i){
        pid_t pid;
        if(!(pid = fork())){
            if constexpr(std::is_same_v<T, parsed_input>){
                if(i > 0){
                    dup2(pipe_array[i - 1][0], STDIN_FILENO);
                    close(pipe_array[i - 1][0]);
                    close(pipe_array[i - 1][1]);
                }
                if(i < num - 1){
                    dup2(pipe_array[i][1], STDOUT_FILENO);
                    close(pipe_array[i][0]);
                    close(pipe_array[i][1]);
                }
                if(input.inputs[i].type == INPUT_TYPE_COMMAND){
                    execute_single_command(input.inputs[i].data.cmd);
                    
                }
                else if(input.inputs[i].type == INPUT_TYPE_SUBSHELL){
                    subshell_execution(input.inputs[i].data.subshell);
                }
            }
            else{
                if(i > 0){
                    dup2(pipe_array[i - 1][0], STDIN_FILENO);
                    close(pipe_array[i - 1][0]);
                    close(pipe_array[i - 1][1]);
                }
                if(i < num - 1){
                    dup2(pipe_array[i][1], STDOUT_FILENO);
                    close(pipe_array[i][0]);
                    close(pipe_array[i][1]);
                }
                execute_single_command(input.commands[i]);  
            }
            exit(EXIT_SUCCESS);
        } 
        else{
            if(i < num - 1){
                close(pipe_array[i][1]);
            }
            child_pids[i] = pid;
            
        }
    }
    for(int i = 0; i < num - 1; ++i){
        close(pipe_array[i][0]);
        
    }
    for(int i = 0; i < num; ++i){
        int status;
        waitpid(child_pids[i], &status, 0);
    }
}

void parallel_execution(parsed_input input, int num, int pipe_array[][2], int child_pids[]){
    if(!pipe_array){
        pid_t child_pids[num];
        for(int i = 0; i < num; ++i){
            pid_t pid;
            if(input.inputs[i].type == INPUT_TYPE_PIPELINE){
                single_input_union pipe_union; pipeline pline;
                pipe_union = input.inputs[i].data;
                pline = pipe_union.pline;
                pipeline_execution(pline, pline.num_commands);
            }
            else if(!(pid = fork())){
                if(input.inputs[i].type == INPUT_TYPE_COMMAND){
                    execute_single_command(input.inputs[i].data.cmd); 
                }
                exit(EXIT_SUCCESS);
            } 
            else{
                child_pids[i] = pid;
            }
        }
        for(int i = 0; i < num; ++i){
            int status;
            waitpid(child_pids[i], &status, 0);
        }
    }
    else{
        for(int i = 0; i < num; ++i){
            pid_t pid_p;
            if(pipe(pipe_array[i]) < 0){
                perror("pipe");
                exit(EXIT_FAILURE);
            }
            if(!(pid_p = fork())){
                dup2(pipe_array[i][0], 0);
                close(pipe_array[i][0]); 
                close(pipe_array[i][1]);
                if(input.inputs[i].type == INPUT_TYPE_COMMAND){
                    execute_single_command(input.inputs[i].data.cmd); 
                }
                else if(input.inputs[i].type == INPUT_TYPE_PIPELINE){
                    single_input_union pipe_union; pipeline pline;
                    pipe_union = input.inputs[i].data;
                    pline = pipe_union.pline;
                    pipeline_execution(pline, pline.num_commands);
                }
                exit(EXIT_SUCCESS);
            } 
            else{
                child_pids[i] = pid_p;
                close(pipe_array[i][0]); 
            }
        }
    }

}

void sequential_execution(parsed_input input, int num){
    for(int i = 0; i < num; i++){
        pid_t pid;
        int status;
        if(input.inputs[i].type == INPUT_TYPE_PIPELINE){
            single_input_union pipe_union; pipeline pline;
            pipe_union = input.inputs[i].data;
            pline = pipe_union.pline;
            pipeline_execution(pline, pline.num_commands);
        }
        else if(!(pid = fork())){
            if(input.inputs[i].type == INPUT_TYPE_COMMAND){
                execute_single_command(input.inputs[i].data.cmd);
            } 
            exit(0);
        }
        else{
            waitpid(pid,&status,0);
        }
        
    }
}

int main(){
    char input[INPUT_BUFFER_SIZE];
    parsed_input input_parsed;
    signal(SIGPIPE, SIG_IGN);
    while(true){
        cout << "/> ";
        cin.getline(input, INPUT_BUFFER_SIZE);
        
        if(strcmp(input, "quit") == 0){
            break;
        }
        if(parse_line(input, &input_parsed)){
            if(input_parsed.num_inputs == 1 && input_parsed.inputs[0].type == INPUT_TYPE_COMMAND){
                execute_single_command(input_parsed.inputs[0].data.cmd);
            }
            else if(input_parsed.separator == SEPARATOR_SEQ){
                sequential_execution(input_parsed, input_parsed.num_inputs);
            }
            else if(input_parsed.separator == SEPARATOR_PIPE){
                pipeline_execution(input_parsed, input_parsed.num_inputs);
            }
            else if(input_parsed.separator == SEPARATOR_PARA){
                parallel_execution(input_parsed, input_parsed.num_inputs, NULL, NULL);
            }
            else if(input_parsed.inputs[0].type == INPUT_TYPE_SUBSHELL){
                subshell_execution(input_parsed.inputs[0].data.subshell);
            }
            free_parsed_input(&input_parsed);
        }
    }   
    return 0;
}