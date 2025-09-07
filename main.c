#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// 任务结构体
typedef struct task_st {
    void *(*func)(void *);  // 任务函数
    void *arg;              // 任务参数
    struct task_st *next;   // 下一个任务
} task_t;

// 任务队列
typedef struct queue_st {
    task_t *front;          // 队头
    task_t *rear;           // 队尾
    int size;               // 当前任务数
    pthread_mutex_t mutex;  // 队列互斥锁
} queue_t;

// 线程池结构体
typedef struct pool_st {
    pthread_t adminTid;             // 管理者线程ID
    queue_t *taskQueue;             // 任务队列
    int maxTasks;                   // 最大任务数
    pthread_t *workingThrs;         // 工作线程数组
    int maxThrs;                    // 最大工作线程数
    int minThrs;                    // 最小工作线程数
    int busyThrs;                   // 忙碌的线程数
    int aliveThrs;                  // 存活的线程数
    int exitThrs;                   // 待终止的线程数
    int shutdown;                   // 线程池状态: 0运行, 1关闭
    pthread_mutex_t poolMux;        // 线程池互斥锁
    pthread_cond_t queueNotEmpty;   // 队列非空条件变量
    pthread_cond_t queueNotFull;    // 队列非满条件变量
    pthread_mutex_t busyMux;        // 忙碌线程计数互斥锁
} pool_t;

// 队列初始化
static int queueInit(queue_t **q) {
    *q = (queue_t *)malloc(sizeof(queue_t));
    if (*q == NULL) {
        perror("malloc queue failed");
        return -1;
    }
    
    (*q)->front = (*q)->rear = NULL;
    (*q)->size = 0;
    if (pthread_mutex_init(&(*q)->mutex, NULL) != 0) {
        perror("pthread_mutex_init failed");
        free(*q);
        *q = NULL;
        return -1;
    }
    return 0;
}

// 入队操作
static int enqueue(queue_t *q, void *(*func)(void *), void *arg) {
    if (q == NULL) return -1;
    
    task_t *task = (task_t *)malloc(sizeof(task_t));
    if (task == NULL) {
        perror("malloc task failed");
        return -1;
    }
    
    task->func = func;
    task->arg = arg;
    task->next = NULL;
    
    pthread_mutex_lock(&q->mutex);
    
    if (q->rear == NULL) {  // 队列为空
        q->front = q->rear = task;
    } else {
        q->rear->next = task;
        q->rear = task;
    }
    q->size++;
    
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

// 出队操作
static int dequeue(queue_t *q, task_t **task) {
    if (q == NULL || task == NULL) return -1;
    
    pthread_mutex_lock(&q->mutex);
    
    if (q->front == NULL) {  // 队列为空
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    
    *task = q->front;
    q->front = q->front->next;
    if (q->front == NULL) {  // 队列变为空
        q->rear = NULL;
    }
    q->size--;
    
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

// 获取队列大小
static int queueSize(queue_t *q) {
    if (q == NULL) return -1;
    
    int size;
    pthread_mutex_lock(&q->mutex);
    size = q->size;
    pthread_mutex_unlock(&q->mutex);
    return size;
}

// 销毁队列
static void queueDestroy(queue_t *q) {
    if (q == NULL) return;
    
    task_t *tmp;
    pthread_mutex_lock(&q->mutex);
    
    while (q->front != NULL) {
        tmp = q->front;
        q->front = q->front->next;
        free(tmp);
    }
    
    pthread_mutex_unlock(&q->mutex);
    pthread_mutex_destroy(&q->mutex);
    free(q);
}

// 工作线程处理函数
static void *workerThread(void *arg) {
    pool_t *pool = (pool_t *)arg;
    task_t *task;
    
    while (1) {
        pthread_mutex_lock(&pool->poolMux);
        
        // 等待任务或退出信号
        while (queueSize(pool->taskQueue) == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queueNotEmpty, &pool->poolMux);
            
            // 检查是否需要退出
            if (pool->exitThrs > 0) {
                pool->exitThrs--;
                pool->aliveThrs--;
                pthread_mutex_unlock(&pool->poolMux);
                pthread_exit(NULL);
            }
        }
        
        // 如果线程池已关闭，退出
        if (pool->shutdown) {
            pool->aliveThrs--;
            pthread_mutex_unlock(&pool->poolMux);
            pthread_exit(NULL);
        }
        
        // 从队列中获取任务
        dequeue(pool->taskQueue, &task);
        
        // 通知可以添加新任务
        pthread_cond_signal(&pool->queueNotFull);
        pthread_mutex_unlock(&pool->poolMux);
        
        // 执行任务
        pthread_mutex_lock(&pool->busyMux);
        pool->busyThrs++;
        pthread_mutex_unlock(&pool->busyMux);
        
        // 执行任务函数
        if (task != NULL) {
            task->func(task->arg);
            free(task);
        }
        
        // 更新忙碌线程数
        pthread_mutex_lock(&pool->busyMux);
        pool->busyThrs--;
        pthread_mutex_unlock(&pool->busyMux);
    }
    
    return NULL;
}

// 管理者线程函数
static void *adminThread(void *arg) {
    pool_t *pool = (pool_t *)arg;
    
    while (!pool->shutdown) {
        // 定期检查，每3秒一次
        sleep(3);
        
        pthread_mutex_lock(&pool->poolMux);
        int queueSize = queueSize(pool->taskQueue);
        int busy = pool->busyThrs;
        int alive = pool->aliveThrs;
        pthread_mutex_unlock(&pool->poolMux);
        
        // 打印调试信息
        printf("Admin: Queue size=%d, Busy=%d, Alive=%d\n", queueSize, busy, alive);
        
        // 增加线程：当所有存活线程都在忙碌且队列不为空
        if (busy == alive && queueSize > 0 && alive < pool->maxThrs) {
            pthread_mutex_lock(&pool->poolMux);
            int add = 0;
            
            // 找到空的线程位置
            for (int i = 0; i < pool->maxThrs && add < 5 && pool->aliveThrs < pool->maxThrs; i++) {
                if (pool->workingThrs[i] == 0) {
                    if (pthread_create(&pool->workingThrs[i], NULL, workerThread, pool) == 0) {
                        pool->aliveThrs++;
                        add++;
                        printf("Added a new worker thread, total alive: %d\n", pool->aliveThrs);
                    }
                }
            }
            pthread_mutex_unlock(&pool->poolMux);
        }
        
        // 减少线程：当忙碌线程远少于存活线程且队列为空
        if (busy * 2 < alive && queueSize == 0 && alive > pool->minThrs) {
            pthread_mutex_lock(&pool->poolMux);
            int reduce = (alive - pool->minThrs) > 5 ? 5 : (alive - pool->minThrs);
            
            if (reduce > 0) {
                pool->exitThrs = reduce;
                // 唤醒等待的线程，让它们退出
                for (int i = 0; i < reduce; i++) {
                    pthread_cond_signal(&pool->queueNotEmpty);
                }
                printf("Will reduce %d worker threads\n", reduce);
            }
            pthread_mutex_unlock(&pool->poolMux);
        }
    }
    
    return NULL;
}

// 初始化线程池
int poolInit(pool_t **p, int maxThrs, int minThrs, int maxTasks) {
    if (p == NULL || maxThrs <= 0 || minThrs <= 0 || maxThrs < minThrs || maxTasks <= 0) {
        return -1;
    }
    
    *p = (pool_t *)malloc(sizeof(pool_t));
    if (*p == NULL) {
        perror("malloc pool failed");
        return -1;
    }
    memset(*p, 0, sizeof(pool_t));
    
    // 初始化任务队列
    if (queueInit(&(*p)->taskQueue) != 0) {
        free(*p);
        *p = NULL;
        return -1;
    }
    
    // 初始化线程数组
    (*p)->workingThrs = (pthread_t *)calloc(maxThrs, sizeof(pthread_t));
    if ((*p)->workingThrs == NULL) {
        perror("calloc workingThrs failed");
        queueDestroy((*p)->taskQueue);
        free(*p);
        *p = NULL;
        return -1;
    }
    
    // 设置线程池参数
    (*p)->maxThrs = maxThrs;
    (*p)->minThrs = minThrs;
    (*p)->maxTasks = maxTasks;
    (*p)->shutdown = 0;
    
    // 初始化互斥锁和条件变量
    if (pthread_mutex_init(&(*p)->poolMux, NULL) != 0 ||
        pthread_mutex_init(&(*p)->busyMux, NULL) != 0 ||
        pthread_cond_init(&(*p)->queueNotEmpty, NULL) != 0 ||
        pthread_cond_init(&(*p)->queueNotFull, NULL) != 0) {
        perror("init mutex or cond failed");
        // 清理已初始化的资源
        pthread_mutex_destroy(&(*p)->poolMux);
        pthread_mutex_destroy(&(*p)->busyMux);
        pthread_cond_destroy(&(*p)->queueNotEmpty);
        pthread_cond_destroy(&(*p)->queueNotFull);
        free((*p)->workingThrs);
        queueDestroy((*p)->taskQueue);
        free(*p);
        *p = NULL;
        return -1;
    }
    
    // 创建初始工作线程
    pthread_mutex_lock(&(*p)->poolMux);
    for (int i = 0; i < minThrs; i++) {
        if (pthread_create(&(*p)->workingThrs[i], NULL, workerThread, *p) == 0) {
            (*p)->aliveThrs++;
        } else {
            perror("create worker thread failed");
        }
    }
    pthread_mutex_unlock(&(*p)->poolMux);
    
    // 创建管理者线程
    if (pthread_create(&(*p)->adminTid, NULL, adminThread, *p) != 0) {
        perror("create admin thread failed");
        // 清理已创建的资源
        (*p)->shutdown = 1;
        pthread_cond_broadcast(&(*p)->queueNotEmpty);
        // 等待工作线程退出
        for (int i = 0; i < maxThrs; i++) {
            if ((*p)->workingThrs[i] != 0) {
                pthread_join((*p)->workingThrs[i], NULL);
            }
        }
        // 销毁其他资源
        pthread_mutex_destroy(&(*p)->poolMux);
        pthread_mutex_destroy(&(*p)->busyMux);
        pthread_cond_destroy(&(*p)->queueNotEmpty);
        pthread_cond_destroy(&(*p)->queueNotFull);
        free((*p)->workingThrs);
        queueDestroy((*p)->taskQueue);
        free(*p);
        *p = NULL;
        return -1;
    }
    
    printf("Thread pool initialized successfully, initial workers: %d\n", (*p)->aliveThrs);
    return 0;
}

// 添加任务到线程池
int addTask(pool_t *p, void *(*func)(void *), void *arg) {
    if (p == NULL || func == NULL || p->shutdown) {
        return -1;
    }
    
    pthread_mutex_lock(&p->poolMux);
    
    // 如果队列已满，等待
    while (queueSize(p->taskQueue) >= p->maxTasks && !p->shutdown) {
        pthread_cond_wait(&p->queueNotFull, &p->poolMux);
    }
    
    // 如果线程池已关闭，返回错误
    if (p->shutdown) {
        pthread_mutex_unlock(&p->poolMux);
        return -1;
    }
    
    // 添加任务到队列
    if (enqueue(p->taskQueue, func, arg) != 0) {
        pthread_mutex_unlock(&p->poolMux);
        return -1;
    }
    
    // 通知工作线程有新任务
    pthread_cond_signal(&p->queueNotEmpty);
    pthread_mutex_unlock(&p->poolMux);
    
    return 0;
}

// 销毁线程池
void poolDestroy(pool_t *p) {
    if (p == NULL || p->shutdown) {
        return;
    }
    
    // 设置关闭标志
    pthread_mutex_lock(&p->poolMux);
    p->shutdown = 1;
    pthread_mutex_unlock(&p->poolMux);
    
    // 唤醒所有等待的工作线程
    pthread_cond_broadcast(&p->queueNotEmpty);
    
    // 等待管理者线程退出
    if (p->adminTid != 0) {
        pthread_join(p->adminTid, NULL);
        p->adminTid = 0;
    }
    
    // 等待所有工作线程退出
    for (int i = 0; i < p->maxThrs; i++) {
        if (p->workingThrs[i] != 0) {
            pthread_join(p->workingThrs[i], NULL);
            p->workingThrs[i] = 0;
        }
    }
    
    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&p->poolMux);
    pthread_mutex_destroy(&p->busyMux);
    pthread_cond_destroy(&p->queueNotEmpty);
    pthread_cond_destroy(&p->queueNotFull);
    
    // 销毁任务队列
    queueDestroy(p->taskQueue);
    
    // 释放工作线程数组
    free(p->workingThrs);
    p->workingThrs = NULL;
    
    // 释放线程池结构体
    free(p);
    printf("Thread pool destroyed successfully\n");
}

// 示例任务函数
void *exampleTask(void *arg) {
    int taskId = *(int *)arg;
    free(arg);  // 释放传递的参数
    
    printf("Task %d started\n", taskId);
    // 模拟任务执行时间
    sleep(2);
    printf("Task %d completed\n", taskId);
    
    return NULL;
}

// 测试程序
int main() {
    pool_t *pool;
    // 初始化线程池：最大10线程，最小3线程，最大20任务
    if (poolInit(&pool, 10, 3, 20) != 0) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return 1;
    }
    
    // 添加示例任务
    for (int i = 0; i < 15; i++) {
        int *taskId = (int *)malloc(sizeof(int));
        *taskId = i;
        addTask(pool, exampleTask, taskId);
        printf("Added task %d\n", i);
    }
    
    // 等待所有任务完成
    sleep(10);
    
    // 销毁线程池
    poolDestroy(pool);
    
    return 0;
}
