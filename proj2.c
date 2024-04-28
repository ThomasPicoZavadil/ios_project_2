#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>
#include <stdbool.h>
#include <asm-generic/fcntl.h>

#define NUM_STOPS 10

int Rcount = 0;
int Rfinished = 0;

sem_t *bus_arrival, *bus_depart, *bus_finish;

typedef enum
{
    on_breakfest,
    on_stop,
    in_bus,
    in_finish,
} RiderState;

struct SharedData
{
    // rider data
    // int riderID;
    // int stop_id;
    // RiderState state;
    // bus data
    int riders_on_stop[NUM_STOPS];
    int current_amount;
    int current_stop;
};

int rand_range(int min, int max)
{
    return min + rand() % (max - min + 1);
}

void semaphores_init()
{
    bus_arrival = sem_open("/bus_arrival", O_CREAT | O_EXCL, 0644, 0);
    if (bus_arrival == SEM_FAILED)
    {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    bus_depart = sem_open("/bus_depart", O_CREAT | O_EXCL, 0644, 0);
    if (bus_depart == SEM_FAILED)
    {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }

    bus_finish = sem_open("/bus_finish", O_CREAT | O_EXCL, 0644, 0);
    if (bus_finish == SEM_FAILED)
    {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
}

int shared_memory()
{
    ///////////SHARED MEMORY KEY - CREATE///////////
    key_t key;
    int shm_id;

    if ((key = ftok("/tmp", 'S')) == -1)
    {
        fprintf(stderr, "ftok");
        exit(EXIT_FAILURE);
    }
    ///////////SHARED MEMORY KEY - CREATE///////////

    shm_id = shmget(key, sizeof(struct SharedData), IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    return shm_id;
}

// Bus process
void bus(int shmid, int stops, int capacity, int travel_time, int riders_amount)
{
    printf("BUS SUCCESS\n");

    ///////////BUS MEMORY - ATTACH///////////
    struct SharedData *shared_data = (struct SharedData *)shmat(shmid, NULL, 0);
    if (shared_data == (struct RiderData *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    ///////////BUS MEMORY - ATTACH///////////

    shared_data->current_amount = 0;
    shared_data->current_stop = 1;

    while (Rfinished <= riders_amount)
    {
        // Simulate travel time to the next stop
        sleep(rand_range(1000, travel_time));
        while (shared_data->current_amount < capacity && shared_data->riders_on_stop[shared_data->current_stop] > 0)
        {
            printf("Bus: Arriving at stop %d\n", shared_data->current_stop);
            sem_post(bus_arrival);
        }
        sem_wait(bus_arrival);
        printf("Bus: Departing from stop %d\n", shared_data->current_stop);
        shared_data->current_stop++;
        if (shared_data->current_stop > stops)
        {
            printf("Bus: Arriving at last stop\n");
            while (shared_data->current_amount > 0)
            {
                sem_post(bus_finish);
            }
        }
    }
    shmdt(shared_data);
    exit(0);
}

// Rider process
void rider(int shmid, int stop_id, int riderID, int breakfest_time)
{
    printf("RIDER SUCCESS\n");

    ///////////RIDER MEMORY - ATTACH///////////
    struct SharedData *shared_data = (struct SharedData *)shmat(shmid, NULL, 0);
    if (shared_data == (struct SharedData *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    ///////////RIDER MEMORY - ATTACH///////////

    // Simulate breakfest
    RiderState state = on_breakfest;
    sleep(rand_range(0, breakfest_time));
    RiderState state = on_stop;
    printf("Rider %d: Waiting at stop %d\n", riderID, stop_id);

    while (state == on_stop)
    {
        sem_wait(bus_arrival);
        if (shared_data->current_stop = stop_id && state == on_stop)
        {
            state = in_bus;
            shared_data->riders_on_stop[shared_data->current_stop]--;
            shared_data->current_amount++;
        }
        sem_post(bus_arrival);
    }
    while (state == in_bus)
    {
        sem_wait(bus_finish);
        shared_data->current_amount--;
        sem_post(bus_finish);
    }

    shmdt(shared_data);
    exit(0);
}

int rider_generator(int shmid, int riders_amount, int breakfest_time, int stops)
{
    struct SharedData *shared_data = (struct SharedData *)shmat(shmid, NULL, 0);
    if (shared_data == (struct SharedData *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < riders_amount; i++)
    { // Create rider processes
        Rcount++;
        int riderID = Rcount;
        int stop_id = rand_range(1, stops);
        pid_t rider_pid = fork();
        if (rider_pid == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        else if (rider_pid == 0)
        {
            rider(shmid, stop_id, riderID, breakfest_time);
        }
    }
}

void main_process(int shmid, int riders_amount, int stops, int capacity, int breakfest_time, int travel_time)
{
    ///////////SHARED MEMORY - ATTACH///////////
    struct SharedData *shared_data = (struct SharedData *)shmat(shmid, NULL, 0);
    if (shared_data == (struct SharedData *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    ///////////SHARED MEMORY - ATTACH///////////

    // Create bus process
    pid_t bus_pid = fork();
    if (bus_pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (bus_pid == 0)
    {
        bus(shmid, stops, capacity, travel_time, riders_amount);
    }

    // Create rider processes
    rider_generator(shmid, riders_amount, breakfest_time, stops);

    // Wait for all child processes to finish
    for (int i = 0; i < riders_amount + 1; i++)
    {
        wait(NULL);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        printf("Spatny pocet argumentu! Pouzijte: ./proj2 L Z K TL TB");
        return 1;
    }
    int L = atoi(argv[1]);  // Počet lyžařů
    int Z = atoi(argv[2]);  // Počet zastávek
    int K = atoi(argv[3]);  // Kapacita autobusu
    int TL = atoi(argv[4]); // Maximální čekací doba lyžařů
    int TB = atoi(argv[5]); // Maximální doba jízdy autobusu
    if (L < 0 || L >= 20000 || Z < 1 || Z > 10 || K < 10 || K > 100 || TL < 0 || TL > 10000 || TB < 0 || TB > 1000)
    {
        printf("Neplatne hodnoty argumentu!\n");
        return 1;
    }

    // Seed the random number generator
    srand(time(NULL));

    ///////////SHARED MEMORY - CREATE///////////
    int shmid = shared_memory();
    ///////////SHARED MEMORY - CREATE///////////

    ///////////SEMAPHORES INIT///////////
    semaphores_init();
    ///////////SEMAPHORES INIT///////////

    main_process(shmid, L, Z, K, TL, TB);

    // Cleanup
    sem_destroy(bus_arrival);
    sem_destroy(bus_depart);
    sem_destroy(bus_finish);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}