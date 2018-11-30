//
// Created by 江泽群 on 2017/11/14.
//
#include "mythread.h"
#define TRUE 1
#define FALSE 0

int next_mythread_t = 0;

sigset_t sig;

void append(struct mythread * thread){
  if(first == end && first == NULL){
    first = end = thread;
  }else{
    end = end->next = thread;
  }
}

void del(struct mythread* d_mythread){
    if(d_mythread != first){
      struct mythread* p_mythread = first;
      for(;p_mythread != NULL && p_mythread->next != d_mythread; p_mythread = p_mythread->next);
      p_mythread->next = d_mythread->next;
      if(d_mythread == end){
        end = p_mythread;
      }
      current = d_mythread->next == NULL? d_mythread->next: first;
    }else{
      first = d_mythread->next;
      d_mythread->next = NULL;
      if(d_mythread == end){
        end = first;
      }
      current = first;
    }
    //从链表中移除指定的线程控制块并修改first和end指针
    if(d_mythread != main_thread){
      free(d_mythread->tContext.uc_stack.ss_sp);
      free(d_mythread);
    }
    //若不为子线程，立即释放线程控制块的资源
}

void mythread_schedule(){
  struct mythread* prev_thread = current;
  struct mythread* temp = NULL;

  current = (current->next == NULL) ? first: current->next;
  //将current指针指向下一个线程控制块
  while(TRUE){
    if(current == NULL){
      current = main_thread;
      //当链表已经为空而且主线程已经退出时
      break;
    }else if(current->state == NEWTHREAD){
      current->state = RUNNING;
      break;
    }else if(current->state == RUNNING){
      //当前线程为可运行状态
      break;
    }else if(current->state == CANCEL || current->state == EXITED || current->state == COMPLETE){
      del(current);
      //当前线程状态处于CANEL、EXITED、COMPLETE需要将其线程控制块释放
    }else{
      current = (current->next == NULL) ? first: current->next;
      //将current指针指向下一个线程控制块
    }
  }

  if(prev_thread == NULL){
    setcontext(&(current->tContext));
    //若前一线程资源已经被释放，直接设置新的上下文即可
  }else{
    if(swapcontext(&(prev_thread->tContext),&(current->tContext)) == -1){
      perror("swapcontext fail.");
    }
    //保存当前上下文至前一线程控制块中，加载新线程的上下文信息
  }
}

void mythread_exit(void *retval){
    current->state = EXITED;
    //修改线程状态为EXITED
    if(retval != NULL){
     retval = current->retval;
    }
    //将retval指向线程函数的返回值
    if(current == main_thread){
      while(!(first == end && first == NULL)){
        mythread_schedule(0);
      }
      //主线程退出后等待所有子线程结束
      exit(0);
    }else{
      mythread_schedule(0);
      //子线程主动退出后立即切至调度函数
    }
  }

int  mythread_equal(mythread_t t1, mythread_t t2){
    if(t1 == t2){
        return 1;
    }else{
        return 0;
    }
}

mythread_t mythread_self(void){
    return current->tID;
}

void mythread_yield(void){
    mythread_schedule(0);
    //线程主动放弃即直接调用调度函数
}

int  mythread_cancel(mythread_t thread){
    struct mythread * p_mythread = first;
    for(; p_mythread != NULL; p_mythread = p_mythread->next){
        if(p_mythread->tID == thread){
            p_mythread->state = CANCEL;
            return 0;
        }
    }
    //寻找对应tID的线程控制块并设置其线程状态为CANCEL
    return -1;
}

void mythread_running(void* (*start_routine)(void*), void *arg){
    current->state = RUNNING;
    //修改线程的状态为RUNNING
    current->retval = start_routine(arg);
    //将线程函数的返回函数存储到retval中
    current->state = COMPLETE;
    //修改线程的状态为COMPLETE
}

int  mythread_create(mythread_t *thread, void* (*start_routine)(void*), void *arg){
    struct mythread* temp = (struct mythread*) malloc(sizeof(struct mythread));
    if(temp == NULL){
      return -1;
    }
    //申请新的线程控制块
    *thread = next_mythread_t;
    temp->tID = next_mythread_t++;
    temp->state = NEWTHREAD;
    temp->next = NULL;
    //初始化新线程的线程控制块的信息
    getcontext(&(temp->tContext));

    temp->tContext.uc_stack.ss_sp = (char*)malloc(STACKCAPACITY);
    if(temp->tContext.uc_stack.ss_sp == NULL){
      return -1;
    }
    temp->tContext.uc_stack.ss_size = STACKCAPACITY;
    temp->tContext.uc_stack.ss_flags = 0;
    temp->tContext.uc_link = &(main_thread->tContext);
    //设置新新线程的线程运行环境
    makecontext(&(temp->tContext), mythread_running, 2,start_routine, arg);
    //指定线程要运行的函数以及函数所需要参数
    append(temp);
    //将线程控制块追加到链表的末端
    return 1;
}


int  mythread_join(mythread_t thread, void **status) {
    struct mythread *p_mythread = first;

    for (; p_mythread != NULL && p_mythread->tID != thread; p_mythread = p_mythread->next);

    if (p_mythread == NULL || p_mythread == current){
        return -1;
    }
    //寻找对应tID的线程控制块
    while (TRUE){
        if (p_mythread == NULL || p_mythread->state == COMPLETE || p_mythread->state == EXITED || p_mythread->state == CANCEL){
            break;
        }
        mythread_schedule(0);
    }
    //等待对应的线程被释放或者结束
    if(status != NULL){
      if(p_mythread != NULL){
        *status = p_mythread->retval;
      }else{
        *status = NULL;
      }
    }
    //等待对应的线程被释放或者处于COMPLETE、EXTIED、CANCEL状态并同时获得线程函数返回值
    return 0;
}

void mythread_init(long period){
    struct sigaction act;

    act.sa_handler = mythread_schedule;
    act.sa_flags  = 0;

    sigemptyset(&act.sa_mask);
    if(sigaction(SIGPROF, &act, NULL) == -1){
        perror("fail to register signal.");
    }
    //设置信号处理函数
    struct itimerval val;

    val.it_value.tv_sec = 0;
    val.it_value.tv_usec = period;

    val.it_interval.tv_sec = 0;
    val.it_interval.tv_usec = period;

    if(setitimer(ITIMER_PROF, &val, NULL) == -1) {
        perror("fail to register timer.");
    }
    //设置定时器
    main_thread = current = (struct mythread*)malloc(sizeof(struct mythread));
    append(main_thread);

    main_thread->tID = next_mythread_t++;
    main_thread->state = RUNNING;
    main_thread->retval = NULL;
    main_thread->next = NULL;

    getcontext(&(main_thread->tContext));
    //设置主函数为0号线程并且设置其运行环境
    sigemptyset(&sig);
    sigaddset(&sig,SIGPROF);
}

int  mythread_mutex_init(mythread_mutex_t *mutex){
    sigprocmask(SIG_BLOCK, &sig, NULL);
    mutex->lock = 1;
    sigprocmask(SIG_UNBLOCK, &sig, NULL);
    //将锁的初始值置为1
    return 1;
}

int  mythread_mutex_lock(mythread_mutex_t *mutex){
    sigprocmask(SIG_BLOCK, &sig, NULL);
    if(mutex->lock == 1){
        mutex->lock = 0;
        mutex->owner = current->tID;
        sigprocmask(SIG_UNBLOCK, &sig, NULL);
        //若当前锁未被加锁将值置位0
    }else{
        current->state = BLOCKED;
        //若当前锁已被加锁，则将当前线程的状态设置为BLOCK
        while(mutex->lock != 1){
          sigprocmask(SIG_UNBLOCK, &sig, NULL);
          mythread_schedule(0);
          sigprocmask(SIG_BLOCK, &sig, NULL);
        }
        //等待锁被释放
        current->state = RUNNING;
        mutex->lock = 0;
        mutex->owner = current->tID;
        sigprocmask(SIG_UNBLOCK, &sig, NULL);
        //锁被释放后进行加锁
    }
    return 0;
}

int  mythread_mutex_unlock(mythread_mutex_t *mutex){
    sigprocmask(SIG_BLOCK, &sig, NULL);
    if(current->tID == mutex->owner){
        mutex->lock = 1;
        sigprocmask(SIG_UNBLOCK, &sig, NULL);
        return 0;
        //判断是否由锁所属线程进行解锁
    }
    return -1;
}
