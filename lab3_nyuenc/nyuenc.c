//
// Created by Xiao Ma on 11/1/21.
//
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stddef.h>
#include <signal.h>

//define the task processed by thread
typedef struct task {
    size_t id;                  //in order id
    void (*t_func)(void *);     //rle will be passed
    void *t_arg;                //argu of t_func
    struct task *next;
} task_t;
//input argument package
typedef struct input_arg {
    size_t len;                 //string length
    char *in_s;                 //input string
    char *out_s;                //output string by rle
    char buf_tail;              //the tail string of each task
    size_t tail_len;            //tail letter length of each task
}input_arg;
//rle result package
typedef struct rle_result{
    size_t id;
    char *out_s;                //the result string after rle
    struct rle_result *next;
    char buf_tail;              //the last letter of the string
    size_t tail_len;            //the last letter length of the string

}rle_res;
//
typedef struct pool {
    pthread_mutex_t thrd_mutex;     //lock of changing the idle and work num
    pthread_mutex_t res_L_mutex;    //lock of result list
    pthread_mutex_t q_mutex;        //lock of task queue

    pthread_cond_t thrd_idle;       //there is an idle thread
    pthread_cond_t q_not_empty;   //there is task
    pthread_cond_t res_L_not_empty;   //there is result task

    pthread_t *threads;
    int max_thrds;       //max number threads of the pool
    size_t work_num;     //the num of the threads which are working
    size_t idle_num;     //the num of the threads which are idle

//    size_t q_len;           //len of task queue
    task_t *q_front;
    task_t *q_rear;
    size_t task_num;        //total task num
    size_t task_over_num;   //task num of processed by rle
    size_t added_task_num;  //had been added to the queue

    size_t res_len;         //the result list length
    size_t merge_over_num;  //the num of task that has been merged
    rle_res *res_first;     //

//    rle_res *res_tail;

    int shutdown;
} thrd_pool;

void rle_enc(void *arg) {
    input_arg *in = (input_arg*) arg;
//    printf("start rle: len = %zu\n",in->len);

    size_t index = 0;
    char* dest = (char*)malloc(sizeof(char) * (in->len * 2 + 1));
    size_t rLen = 1;
    if (in->in_s[0] == '\0') return; //empty string
    /* traverse the input string one by one */

    for (size_t i = 0; i < in->len-1; i++) {
        if (in->in_s[i] == in->in_s[i+1]) {
            rLen++;
        }
        else {
            dest[index++] = in->in_s[i];
            dest[index++] = (char)rLen;
            rLen = 1;
        }
    }

    in->buf_tail = in->in_s[in->len-1];
    in->tail_len = rLen;
    dest[index] = '\0';
    in->out_s = dest;
//    printf("\ndest = %s, buftail = %c tail_len = %c\n",in->out_s, in->buf_tail,in->tail_len);
}

//add task to the task queue
int addTask(thrd_pool *pool, void (*t_func)(void *),void *t_arg) {

    task_t *t = (task_t *) malloc(sizeof(task_t));
    t->t_func = t_func;
    t->t_arg = t_arg;
    t->next = NULL;

    //lock the task queue
    pthread_mutex_lock(&pool->q_mutex);
//    pool->idle_num++;

    //add the task to the rear
    if (pool->q_front == NULL) {
        pool->q_front = pool->q_rear = t;
    }
    else {
        pool->q_rear->next = t;
        pool->q_rear = t;
    }

    t->id = pool->added_task_num++;
//    pool->q_len++;
//    printf(" over add task:  t_id = %zu\n",t->id);

    pthread_cond_signal(&pool->q_not_empty);
    pthread_mutex_unlock(&pool->q_mutex);

    return 0;
}

//merge the rle result from the result list
void mergeResult(thrd_pool *pool) {
    char prev_tail;
    size_t prev_count;
    int flag= 0;

    while(pool->merge_over_num < pool->task_num) {
        pthread_mutex_lock(&pool->res_L_mutex);
        while(pool->res_first == NULL && pool->shutdown != 1){
            pthread_cond_wait(&pool->res_L_not_empty,&pool->res_L_mutex);
//            printf("        main thread is waiting : \n");
//            sleep(1);
        }
        if (pool->task_num == pool->merge_over_num)
            break;

        while(pool->res_first != NULL && pool->res_first->id == pool->merge_over_num) {
            if (flag == 0) {
//                printf("** the %zu-th task is going to merge = %s\n",pool->res_first->id,pool->res_first->out_s);
                printf("%s",pool->res_first->out_s);
                fflush(stdout);
                flag = 1;
            }
            else {
                if (prev_tail == pool->res_first->out_s[0]) {
                    size_t count = prev_count + pool->res_first->out_s[1];
                    pool->res_first->out_s[1] = (char)count;
//                    printf("\nthe %zu-th task is going to merge = %s\n",pool->res_first->id,pool->res_first->out_s);
                    printf("%s",pool->res_first->out_s);
                    fflush(stdout);

                }
                else {
//                    printf("\nthe %zu-th task is going to merge = %s\n",pool->res_first->id,pool->res_first->out_s);
                    printf("%c%c", prev_tail, prev_count);
//                    fflush(stdout);
                    printf("%s",pool->res_first->out_s);
                    fflush(stdout);
                }
            }

            pool->merge_over_num++;
            pool->res_len--;
//            printf("merge_over_num = %zu",pool->merge_over_num);
            prev_tail = pool->res_first->buf_tail;
            prev_count = pool->res_first->tail_len;

            pool->res_first = pool->res_first->next;

        }

        pthread_mutex_unlock(&pool->res_L_mutex);


    }
//    pool->shutdown = 1;

    printf("%c%c",prev_tail,(char)prev_count);
    fflush(stdout);


}

//process the task, rle, store to result list
void *consumer(void *t_pool) {
    thrd_pool  *pool = (thrd_pool*)t_pool;
    task_t  *t;

    while (1) {
        pthread_mutex_lock(&pool->q_mutex);
        while(pool->q_front == NULL && pool->shutdown != 1) {
//            printf("thread 0x%lx is waiting task, idle = %zu\n", (unsigned long)pthread_self(),pool->idle_num);
            pthread_cond_wait(&pool->q_not_empty,&pool->q_mutex);
        }
//        printf("over waited\n");

        //dispatch to thread
        t = pool->q_front;

        if (t->next == NULL)
            pool->q_front = pool->q_rear = NULL;
        else
            pool->q_front = pool->q_front->next;


        pthread_mutex_lock(&pool->thrd_mutex);
        pool->idle_num--;
        pool->work_num++;
        pthread_mutex_unlock(&pool->thrd_mutex);
//        printf("thread 0x%lx start working,idle = %zu, w_num = %zu \n",
//               (unsigned long)pthread_self(),pool->idle_num,pool->work_num);
        pthread_mutex_unlock(&pool->q_mutex);

        t->t_func(t->t_arg);
        pool->task_over_num++;

        input_arg *in = (input_arg*)t->t_arg;\

        rle_res *res = malloc(sizeof(rle_res));
        res->id = t->id;
        res->out_s = in->out_s;
        res->buf_tail = in->buf_tail;
        res->tail_len = in->tail_len;
        res->next = NULL;

//        printf("over rle task %zu, remain task %zu\n",t->id,pool->q_len);

        pthread_mutex_lock(&pool->res_L_mutex);

        if (pool->res_first == NULL || pool->res_first->id >= res->id) {
            res->next = pool->res_first;
            pool->res_first = res;
        }
        else {
            rle_res *temp = pool->res_first;
            while ((temp->next != NULL) && (temp->next->id < res->id)) {
                temp = temp->next;
            }
            res->next = temp->next;
            temp->next = res;

        }
//        sleep(1);

        pool->res_len++;

        pthread_cond_signal(&pool->res_L_not_empty);
        pthread_mutex_unlock(&pool->res_L_mutex);

        pthread_mutex_lock(&pool->thrd_mutex);
        pool->idle_num++;
        pool->work_num--;
        pthread_mutex_unlock(&pool->thrd_mutex);
//        printf("thread 0x%lx over working,idle = %zu, w_num = %zu \n",
//               (unsigned long)pthread_self(),pool->idle_num,pool->work_num);

        if(pool->q_front==NULL && pool->shutdown == 1 ) {
//            printf("    all tasks are finished,task_over_num = %zu, task_num = %zu\n",pool->task_over_num,pool->task_num);
            break;
        }
    }

    pthread_exit(NULL);
}

//create the pool, init the pool information
thrd_pool *thrd_pool_create(int max_thrds) {

    thrd_pool *pool = (thrd_pool *)malloc(sizeof(thrd_pool));
    //init pool info;
    pool->max_thrds = max_thrds;
    pool->work_num = 0;
    pool->idle_num = max_thrds;

    pool->q_front = pool->q_rear = NULL;
//    pool->q_len = 0;
    pool->task_num = 0;
    pool->task_over_num = 0;
    pool->added_task_num = 0;

    pool->res_first = NULL;  /////////////////////
    pool->res_len = 0;
    pool->merge_over_num = 0;

    pool->shutdown = 0;

    pthread_mutex_init(&pool->thrd_mutex,NULL);
    pthread_mutex_init(&pool->q_mutex,NULL);
    pthread_mutex_init(&pool->res_L_mutex,NULL);
    pthread_cond_init(&pool->q_not_empty,NULL);
    pthread_cond_init(&pool->res_L_not_empty,NULL);

    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * max_thrds);

    for (int i = 0; i < max_thrds; ++i) {
        pthread_create(&pool->threads[i],NULL,consumer,(void*)pool);
//        printf(" idle_num = %zu, w_num = %zu, thread 0x%lx is created \n", pool->idle_num,pool->work_num,(unsigned long)pthread_self());

    }
    return pool;
}
//split file into chunks
void splitChunk(thrd_pool *pool,char *name) {
//    printf("split chunk\n");

    size_t chunk_num = 0;
    int fd  = open(name,O_RDONLY);
    struct stat s;
    fstat(fd, &s);
    chunk_num = s.st_size / 4096;
//    printf("name = %s, s.st_size = %lld, chunk_num = %zu\n",name,s.st_size/4096,chunk_num);
    if (s.st_size % 4096)
        chunk_num++;
    pool->task_num += chunk_num;
//    printf("chunk_num = %zu\n",chunk_num);

    char *addr = (char *) mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    for (int i = 0; i < chunk_num; ++i) {
        input_arg *in = malloc(sizeof(input_arg));
        in->in_s = addr + 4096*i;
        if (i == (chunk_num-1) && s.st_size % 4096)
            in->len = s.st_size-4096*i;
        else
            in->len = 4096;
        addTask(pool,rle_enc,in);
    }

}

int main(int argc, char* argv[]) {

    int opt;
    int jobs = 1;
    while ((opt = getopt(argc, argv, "j:")) != -1) {
        switch (opt) {
            case 'j':
//                printf("Option j has argu: %s\n",optarg);
                jobs = atoi(optarg);
                break;
            default:
                printf("no jobs arg\n");
                break;
        }
    }

    if (optind >= argc) {
        printf("no file argument\n");
    }

    thrd_pool *pool = thrd_pool_create(jobs);
//    printf("start split:\n");

    for (int i = optind; i < argc; ++i) {
        splitChunk(pool,argv[i]);
    }

    mergeResult(pool);

    exit(EXIT_SUCCESS);

}

