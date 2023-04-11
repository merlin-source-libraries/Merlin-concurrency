#include <merlin_thread_pool.hpp>

namespace merl
{
    thread_pool::thread_pool(std::size_t threads_count) : threads_count_(threads_count), alive_(false), running_tasks_count_(0)
    {
        threads_pool_.reserve(threads_count_);
    }
    thread_pool::~thread_pool()
    {
        this->stop(SYNC);
    }

    void thread_pool::start()
    {
        // Do nothing if already alive.
        if(!alive_)
        {
            // Empty the threads pool if it is not
            // i.e. stop() was called with STOP_POLICY::ASYNC.
            if(!threads_pool_.empty())
            {
                this->clean_up();
            }

            // Start
            alive_ = true;
            for(std::size_t i = 0; i < threads_count_; ++i)
                threads_pool_.emplace_back(&thread_pool::spin, this);
        }
    }
    void thread_pool::stop(STOP_POLICY sp)
    {
        {
            std::lock_guard<std::mutex> locker(guardian_); // Lock to make the condition_variable's predicate evaluation safe (let it finish and return before the wait() releases the mutex -> Prevent threads to miss the notify_all()).
            alive_ = false;
        }
        thread_spin_cv_.notify_all();

        if(sp == SYNC && !threads_pool_.empty())
        {
            this->clean_up();
        }
    }
    thread_pool::STATUS thread_pool::status() const
    {
        return (alive_ ? UP : DOWN);
    }
    void thread_pool::join_all()
    {
        if(!alive_ && !threads_pool_.empty())
        {
            this->clean_up();
        }
    }
    void thread_pool::add_task(task * t)
    {
        std::lock_guard<std::mutex> locker(guardian_);
        tasks_pool_.push(t);
        if(alive_)
            thread_spin_cv_.notify_one();
    }
    void thread_pool::clear()
    {
        std::queue<task*> tmp;
        guardian_.lock();
        tasks_pool_.swap(tmp);
        guardian_.unlock();
    }
    bool thread_pool::is_pending() const
    {
        std::lock_guard<std::mutex> locker(guardian_);
        return !tasks_pool_.empty();
    }
    std::size_t thread_pool::pending() const
    {
        std::lock_guard<std::mutex> locker(guardian_);
        return tasks_pool_.size();
    }
    bool thread_pool::is_running() const
    {
        return running_tasks_count_;
    }
    std::size_t thread_pool::running() const
    {
        return running_tasks_count_;
    }
    void thread_pool::wait_for_idle() const
    {
        while(is_pending() || is_running())
            std::this_thread::yield();
    }

    void thread_pool::spin()
    {
        task * t;
        while(alive_)
        {
            std::unique_lock<std::mutex> lk(guardian_);
            thread_spin_cv_.wait(lk, [&](){return !alive_ || !tasks_pool_.empty();});
            
            // At this point, if "alive_ == true" then tasks_pool_ cannot be empty (the lock was reacquired before evaluating the predicate).

            if(alive_)
            {
                t = tasks_pool_.front();
                tasks_pool_.pop();
                lk.unlock();

                ++running_tasks_count_;
                t->run();
                --running_tasks_count_;
            }
            // No need to "else lk.unlock()" here since the std::unique_lock will already release the mutex at destruction.
        }
    }
    void thread_pool::clean_up()
    {
        for(std::thread & th : threads_pool_)
        {
            if(th.joinable())
                th.join();
        }
        threads_pool_.clear();
    }
}
