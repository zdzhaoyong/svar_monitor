#include "Svar/Svar.h"
#include "GSLAM/core/Timer.h"
#include "unistd.h"
#include "CPUMetric.h"
#include "MemoryMetric.h"

extern "C"{
#include "iotop.h"
}

config_t config;
params_t params;

using namespace sv;

std::map<int,Svar>          status;
bool                        inited=false;

int getparent(pid_t pid){
    std::ifstream ifs("/proc/"+std::to_string(pid)+"/stat");
    int _pid,_parent,_skip;
    std::string cmd,state;
    ifs>>_pid>>cmd>>state>>_skip>>_parent;
//    std::cout<<cmd<<"parent of "<<pid<<" is "<<_parent<<std::endl;
    return _parent;
}

int nl_gpid_info(pid_t xxxid, int isp, struct xxxid_stats *stats)
{
    struct pidgen *pg = openpidgen( PIDGEN_FLAGS_TASK);

    if (!pg)
    {
        perror("openpidgen");
        exit(EXIT_FAILURE);
    }

    int pid;
    xxxid=getparent(xxxid);

    stats->read_bytes=0;
    stats->write_bytes=0;
    xxxid_stats state;
    while ((pid = pidgen_next(pg)) > 0)
    {
        if(getparent(pid)!=xxxid) continue;
//            std::cout<<pid<<"!="<<xxxid<<std::endl;//


        if(nl_xxxid_info(pid,0,&state)) continue;

        stats->read_bytes+=state.read_bytes;
        stats->write_bytes+=state.write_bytes;
    }

    closepidgen(pg);
    return 0;
}

Svar get_io_usage(){
    int pid=getpid();
    if(!inited){
        nl_init();
        std::cerr<<"iosvar initialzed."<<std::endl;
        inited=true;
    }


    xxxid_stats state;
    if(nl_gpid_info(pid,0,&state))
        return Svar();

    if(!status.count(pid)){
       status[pid]={GSLAM::TicToc::timestamp(),state};
       return {{"readTotal",(int)state.read_bytes},{"writeTotal",(int)state.write_bytes},
       {"readPerSec",0.},{"writePerSec",0.}};
    }
    else{
        Svar var    =status[pid];
        double current=GSLAM::TicToc::timestamp();
        double time =current-var[0].as<double>();
        var[0]=current;

        xxxid_stats& last=var[1].as<xxxid_stats>();
        double readPerSec=(state.read_bytes-last.read_bytes)/time;
        double writePerSec=(state.write_bytes-last.write_bytes)/time;
        last=state;
        return {{"readTotal",(int)state.read_bytes},{"writeTotal",(int)state.write_bytes},
        {"readPerSec",readPerSec},{"writePerSec",writePerSec},
        {"lastTimeDiff",time}};
    }

}

Svar get_cpu_usage(){
    static GSLAM::CPUMetric metric;
    return metric.usage();
}

Svar get_mem_usage(){
    static GSLAM::MemoryMetric mem;
    return (double)mem.usage();
}

class Monitor{
public:
    Monitor(std::string filepath,double tic=1.):tic(tic){
        workthread=std::thread(&Monitor::work,this,filepath);
    }

    ~Monitor(){
        shouldStop=true;
        workthread.join();
    }

    void work(std::string filepath){
        std::ofstream ofs(filepath);
        ofs.precision(14);
        if(!ofs.is_open()) return;
        shouldStop=false;

        bool isFirst=true;
        while(!shouldStop){
            Svar status={{"time",GSLAM::TicToc::timestamp()},
                         {"cpu",get_cpu_usage()},
                         {"io",get_io_usage()},
                        {"mem",get_mem_usage()}};
            ofs<<std::string(isFirst?"[":",")<<std::endl;
            isFirst=false;
            ofs<<status;
            GSLAM::Rate::sleep(tic);
        }
        ofs<<"]"<<std::endl;
    }

    bool        shouldStop;
    std::thread workthread;
    double      tic;
};


Svar monitor(std::string json_file,double tic){
    return std::make_shared<Monitor>(json_file,tic);
}


REGISTER_SVAR_MODULE(main){
    nl_init();

    svar["__name__"]="monitor";

    svar["get_pid"]=Svar::lambda([](){return (int)getpid();});
    svar["get_io_usage"] = get_io_usage;
    svar["get_cpu_usage"]= get_cpu_usage;
    svar["get_mem_usage"]= get_mem_usage;

    svar["monitor"]= monitor;
}

EXPORT_SVAR_INSTANCE
