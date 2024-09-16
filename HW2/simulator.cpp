#include <iostream>
#include <vector>
#include <string>
#include <pthread.h>
#include <sys/time.h>
#include "monitor.h"
#include "helper.h"
#include "WriteOutput.h"



using namespace std;
int car_num;
int narrow_num;
int ferry_num;
int crossroad_num;



struct Path_components {
    char connectorType;
    int connectorID;
    int from;
    int to;
};

struct Narrow_bridge {
    int travel_time;
    int max_wait_time;
};

struct Ferry {
    int travel_time;
    int max_wait_time;
    int capacity;
};

struct Crossroad {
    int travel_time;
    int max_wait_time;
};

struct Car {
    int ID;
    int travel_time;
    vector<Path_components> path;
};


void parser(vector<Narrow_bridge>& narrow_bridges, vector<Ferry>& ferries, vector<Crossroad>& crossroads, vector<Car>& cars) {
    cin >> narrow_num;
    for (int i = 0; i < narrow_num; i++) {
        Narrow_bridge bridge;
        cin >> bridge.travel_time >> bridge.max_wait_time;
        narrow_bridges.push_back(bridge);
    }

    cin >> ferry_num;
    for (int i = 0; i < ferry_num; i++) {
        Ferry ferry;
        cin >> ferry.travel_time >> ferry.max_wait_time >> ferry.capacity;
        ferries.push_back(ferry);
    }

    cin >> crossroad_num;
    for (int i = 0; i < crossroad_num; i++) {
        Crossroad crossroad;
        cin >> crossroad.travel_time >> crossroad.max_wait_time;
        crossroads.push_back(crossroad);
    }

    
    cin >> car_num;
    for (int i = 0; i < car_num; i++) {
        Car car;
        car.ID = i;
        int path_size;
        cin >> car.travel_time >> path_size;
        for (int j = 0; j < path_size; j++) {
            Path_components path_component;
            string connector;
            cin >> connector >> path_component.from >> path_component.to;
            path_component.connectorType = connector[0];
            path_component.connectorID = std::stoi(connector.substr(1));
            car.path.push_back(path_component);
        }
        cars.push_back(car);
    }
}

class NarrowBridgeMonitor : public Monitor {
private:
    Narrow_bridge bridge;
    pthread_cond_t LanePassing;
    pthread_cond_t max_time_reached[2];
    pthread_cond_t not_empty;
    bool flag[2];
    int passing_line_direction;
    pthread_mutex_t output_mutex;
    int isPassing;
    vector<int> waiting_cars[2];
    
   

public:
    NarrowBridgeMonitor(Narrow_bridge bridge) : bridge(bridge){
        isPassing = 0;
        passing_line_direction = 0;
        pthread_cond_init(&LanePassing, NULL);
        pthread_cond_init(&max_time_reached[0], NULL);
        pthread_cond_init(&max_time_reached[1], NULL);
        pthread_cond_init(&not_empty, NULL);
        pthread_mutex_init(&output_mutex, NULL);
        flag[0] = false;
        flag[1] = false;

    }
    void Pass(int car_id, int connector_id, char connector_type, int direction){
        pthread_mutex_lock(&output_mutex);
        WriteOutput(car_id, connector_type, connector_id, ARRIVE);
        waiting_cars[direction].push_back(car_id);
        
        while(1){
            start:
            if(waiting_cars[direction].front() == car_id) flag[direction] = true;
            if(direction == passing_line_direction){
                if(!waiting_cars[direction].empty() && car_id == waiting_cars[direction].front()){
                    if(isPassing > 0){
                        pthread_mutex_unlock(&output_mutex);
                        sleep_milli(PASS_DELAY);
                        pthread_mutex_lock(&output_mutex);
                        
                    }
                    if(direction != passing_line_direction) goto start;
                    WriteOutput(car_id, connector_type, connector_id, START_PASSING);
                    isPassing++;
                    waiting_cars[direction].erase(waiting_cars[direction].begin());// önce mi sonra mı
                    pthread_cond_broadcast(&LanePassing);
                    pthread_mutex_unlock(&output_mutex);
                    sleep_milli(bridge.travel_time);
                    pthread_mutex_lock(&output_mutex);
                    WriteOutput(car_id, connector_type, connector_id, FINISH_PASSING);
                    isPassing--;
                    if(isPassing == 0){
                        pthread_cond_broadcast(&not_empty);
                        if(waiting_cars[direction].empty()) pthread_cond_broadcast(&max_time_reached[!direction]);

                    }
                    pthread_mutex_unlock(&output_mutex);
                    break;
                }
                else{
                    pthread_cond_wait(&LanePassing, &output_mutex);
                }
                
            }
            else if(isPassing == 0 && waiting_cars[passing_line_direction].empty()){
                    passing_line_direction = direction;
                    
            }
            else if(flag[direction]){            
                flag[direction] = false;
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += bridge.max_wait_time / 1000;
                timeout.tv_nsec += (bridge.max_wait_time % 1000) * 1000000;
                if(timeout.tv_nsec >= 1000000000){
                    timeout.tv_sec++;
                    timeout.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&max_time_reached[direction], &output_mutex, &timeout);

                passing_line_direction = direction;
                while(isPassing > 0){
                    pthread_cond_wait(&not_empty, &output_mutex);
                }

                flag[direction] = false;
                
                 
                
            }
            else{
                pthread_cond_wait(&LanePassing, &output_mutex);
            }
            
        }
        pthread_mutex_unlock(&output_mutex);


    }
};


class FerryMonitor {
private:
    Ferry ferry;
    pthread_cond_t is_full[2];
    pthread_cond_t time_expired[2];
    pthread_cond_t pass_cond;
    pthread_mutex_t mutex;
    int capacity;
    int waiting_car[2];
    bool pass;

public:
    FerryMonitor(Ferry ferry) : ferry(ferry), capacity(ferry.capacity){
        pthread_cond_init(&is_full[0], NULL);
        pthread_cond_init(&is_full[1], NULL);
        pthread_cond_init(&pass_cond, NULL);
        pthread_cond_init(&time_expired[0], NULL);
        pthread_cond_init(&time_expired[1], NULL);
        pthread_mutex_init(&mutex, NULL);
        waiting_car[0] = 0;
        waiting_car[1] = 0;
        pass = false;
    }

    void Pass(int car_id, int connector_id, char connector_type, int direction){
        pthread_mutex_lock(&mutex);
        while(pass){
            pthread_cond_wait(&pass_cond, &mutex);
        }
        waiting_car[direction]++;
        WriteOutput(car_id, connector_type, connector_id, ARRIVE);
        if(waiting_car[direction] == capacity){
            waiting_car[direction]--;
            pass = true;
            WriteOutput(car_id, connector_type, connector_id, START_PASSING);
            pthread_cond_broadcast(&time_expired[direction]);
            if(!waiting_car[direction]){
                pass = false;
                pthread_cond_broadcast(&pass_cond);
            }
            pthread_mutex_unlock(&mutex);
            sleep_milli(ferry.travel_time);
            pthread_mutex_lock(&mutex);
            
            WriteOutput(car_id, connector_type, connector_id, FINISH_PASSING);
        }
        else if(waiting_car[direction] == 1){
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += ferry.max_wait_time / 1000;
            timeout.tv_nsec += (ferry.max_wait_time % 1000) * 1000000;
            if(timeout.tv_nsec >= 1000000000){
                timeout.tv_sec++;
                timeout.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&time_expired[direction], &mutex, &timeout);
            pthread_cond_broadcast(&is_full[direction]);
            waiting_car[direction]--;
            WriteOutput(car_id, connector_type, connector_id, START_PASSING);
            if(!waiting_car[direction]){
                pass = false;
                pthread_cond_broadcast(&pass_cond);
            }
            pthread_mutex_unlock(&mutex);
            sleep_milli(ferry.travel_time);
            pthread_mutex_lock(&mutex);
            WriteOutput(car_id, connector_type, connector_id, FINISH_PASSING);    
        }
        else{
            pthread_cond_wait(&is_full[direction], &mutex);
            waiting_car[direction]--;
            WriteOutput(car_id, connector_type, connector_id, START_PASSING);
            if(!waiting_car[direction]){
                pass = false;
                pthread_cond_broadcast(&pass_cond);
            }
            pthread_mutex_unlock(&mutex);
            sleep_milli(ferry.travel_time);
            pthread_mutex_lock(&mutex);
            WriteOutput(car_id, connector_type, connector_id, FINISH_PASSING);

        }
        pthread_mutex_unlock(&mutex);
    }

};
void check_next_available(vector<int> waiting_cars[4], int& passing_line_direction){
    int i = passing_line_direction;
    for(int j = 0; j < 4; j++){
        
        if(waiting_cars[i].size() > 0){
            passing_line_direction = i;
            break;
        }
        i = (i + 1)%4;
    }
    
}
class CrossroadMonitor : public Monitor {
private:
    Crossroad crossroad;
    pthread_cond_t LanePassing;
    pthread_cond_t max_time_reached[4];
    pthread_cond_t not_empty;
    bool flag[4];
    int passing_line_direction;
    pthread_mutex_t output_mutex;
    int isPassing;
    vector<int> waiting_cars[4];

public:
    CrossroadMonitor(Crossroad crossroad) : crossroad(crossroad) {
        isPassing = 0;
        passing_line_direction = 0;
        pthread_cond_init(&LanePassing, NULL);
        
        pthread_cond_init(&max_time_reached[0], NULL);
        pthread_cond_init(&max_time_reached[1], NULL);
        pthread_cond_init(&max_time_reached[2], NULL);
        pthread_cond_init(&max_time_reached[3], NULL);
        pthread_cond_init(&not_empty, NULL);
        pthread_mutex_init(&output_mutex, NULL);
        flag[0] = false;
        flag[1] = false;
        flag[2] = false;
        flag[3] = false;
    }

   void Pass(int car_id, int connector_id, char connector_type, int direction){
        pthread_mutex_lock(&output_mutex);
        WriteOutput(car_id, connector_type, connector_id, ARRIVE);
        waiting_cars[direction].push_back(car_id);
        while(1){
            start:
            if(waiting_cars[direction].front() == car_id) flag[direction] = true;
            if(direction == passing_line_direction){
                if(!waiting_cars[direction].empty() && car_id == waiting_cars[direction].front()){
                    if(isPassing > 0){
                        pthread_mutex_unlock(&output_mutex);
                        sleep_milli(PASS_DELAY);
                        pthread_mutex_lock(&output_mutex);
                        
                    }
                    if(direction != passing_line_direction) goto start;
                    WriteOutput(car_id, connector_type, connector_id, START_PASSING);
                    isPassing++;
                    waiting_cars[direction].erase(waiting_cars[direction].begin());// önce mi sonra mı
                    pthread_cond_broadcast(&LanePassing);
                    pthread_mutex_unlock(&output_mutex);
                    sleep_milli(crossroad.travel_time);
                    pthread_mutex_lock(&output_mutex);
                    WriteOutput(car_id, connector_type, connector_id, FINISH_PASSING);
                    isPassing--;
                    if(waiting_cars[direction].empty()){
                        check_next_available(waiting_cars, passing_line_direction);
                        pthread_cond_broadcast(&max_time_reached[passing_line_direction]);
                            
                    } 
                    if(isPassing == 0){
                        pthread_cond_broadcast(&not_empty);
                        

                    }
                    pthread_mutex_unlock(&output_mutex);
                    break;
                }
                else{
                    pthread_cond_wait(&LanePassing, &output_mutex);
                }
                
            }
            else if(isPassing == 0 && waiting_cars[passing_line_direction].empty()){
                check_next_available(waiting_cars, passing_line_direction);
                    
            }
            else if(flag[direction]){            
                flag[direction] = false;
                struct timespec timeout;
                clock_gettime(CLOCK_REALTIME, &timeout);
                timeout.tv_sec += crossroad.max_wait_time / 1000;
                timeout.tv_nsec += (crossroad.max_wait_time % 1000) * 1000000;
                if(timeout.tv_nsec >= 1000000000){
                    timeout.tv_sec++;
                    timeout.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&max_time_reached[direction], &output_mutex, &timeout);
                int i = passing_line_direction;
                for(int j = 0; j < 4; j++){
                    i = (i + 1)%4;  
                    if(waiting_cars[i].size() > 0){
                        passing_line_direction = i;
                        break;
                    }
                }
                while(isPassing > 0){
                    pthread_cond_wait(&not_empty, &output_mutex);
                }
                flag[direction] = false;
                
                 
                
            }
            else{
                pthread_cond_wait(&LanePassing, &output_mutex);
            }
            
        }
        pthread_mutex_unlock(&output_mutex);


    }
};
vector<NarrowBridgeMonitor> narrow_monitor;
vector<FerryMonitor> ferry_monitor;
vector<CrossroadMonitor> crossroad_monitor;
void* CarThread(void* arg) {
    Car* car = (Car*)arg;
    int path_size = (car ->path).size();
    int car_id = car ->ID;

    
    for(int i = 0; i < path_size; i++){
        int connector_id =  (car ->path[i]).connectorID;
        char connector_type = (car ->path[i]).connectorType;
        int direction = (car ->path[i]).from;
        WriteOutput(car_id, connector_type, connector_id, TRAVEL);
        sleep_milli(car ->travel_time);
        if(connector_type == 'N') narrow_monitor[connector_id].Pass(car_id,connector_id, connector_type, direction);
        else if (connector_type == 'F') ferry_monitor[connector_id].Pass(car_id,connector_id, connector_type, direction);
        else if(connector_type == 'C') crossroad_monitor[connector_id].Pass(car_id,connector_id, connector_type, direction);
        
    }


    pthread_exit(NULL);
}

int main() {
    vector<Narrow_bridge> narrow_bridges;
    vector<Ferry> ferries;
    vector<Crossroad> crossroads;
    vector<Car> cars;
    
    
    parser(narrow_bridges, ferries, crossroads, cars);
    for(int i = 0 ; i < narrow_num; i++){
        narrow_monitor.push_back(NarrowBridgeMonitor(narrow_bridges[i]));
    }
    for(int i = 0 ; i < ferry_num; i++){
        ferry_monitor.push_back(FerryMonitor(ferries[i]));

    }
    for(int i = 0 ; i < crossroad_num; i++){
        crossroad_monitor.push_back(CrossroadMonitor(crossroads[i]));

    }
    InitWriteOutput();
    
    pthread_t carThreads[car_num];
    for (int i = 0; i < car_num; i++) {
        pthread_create(&carThreads[i], NULL, CarThread, (void*)&cars[i]);
    }

    for (int i = 0; i < car_num; i++){
        pthread_join(carThreads[i], NULL);
    }

    return 0;
}
