#include <bits/stdc++.h>
#include <dirent.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

using namespace std;

struct CPUStat {
    unsigned long long user=0,nicev=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0,guest=0,guest_nice=0;
};
static bool readCPU(CPUStat& s){
    ifstream f("/proc/stat"); if(!f) return false;
    string cpu; f>>cpu; if(cpu!="cpu") return false;
    f>>s.user>>s.nicev>>s.system>>s.idle>>s.iowait>>s.irq>>s.softirq>>s.steal>>s.guest>>s.guest_nice; return true;
}
static unsigned long long totalJiff(const CPUStat& s){
    return s.user+s.nicev+s.system+s.idle+s.iowait+s.irq+s.softirq+s.steal+s.guest+s.guest_nice;
}
static unsigned long long idleJiff(const CPUStat& s){ return s.idle + s.iowait; }

struct MemInfo { unsigned long long totalKB=0, availKB=0; };
static MemInfo readMem(){
    MemInfo mi; ifstream f("/proc/meminfo"); string k; unsigned long long v; string unit;
    while(f>>k>>v>>unit){
        if(k=="MemTotal:") mi.totalKB=v;
        else if(k=="MemAvailable:") mi.availKB=v;
    }
    return mi;
}

struct ProcPrev { unsigned long long procTotal=0; }; // utime+stime last
struct ProcInfo {
    int pid=0; string name; double cpu=0.0, mem=0.0;
    unsigned long long rssKB=0;
};

static bool is_digits(const string& s){ return !s.empty() && all_of(s.begin(),s.end(),::isdigit); }

static vector<int> listPids(){
    vector<int> pids; DIR* d=opendir("/proc"); if(!d) return pids;
    dirent* e; while((e=readdir(d))){
        if(e->d_type==DT_DIR){
            string n=e->d_name;
            if(is_digits(n)) pids.push_back(stoi(n));
        }
    } closedir(d); return pids;
}

static bool readStatForPid(int pid, string& comm, unsigned long long& utime, unsigned long long& stime, long& rss){
    string path="/proc/"+to_string(pid)+"/stat"; ifstream f(path); if(!f) return false;
    // comm can contain spaces in parentheses. Read whole line then parse carefully.
    string line; getline(f,line);
    // find last ')'
    size_t lparen=line.find('('), rparen=line.rfind(')');
    if(lparen==string::npos||rparen==string::npos||rparen<=lparen) return false;
    comm=line.substr(lparen+1, rparen-lparen-1);
    // split remaining after rparen+2 (skip space)
    string rest=line.substr(rparen+2); // starts at field 3
    // fields: see man proc; utime=14, stime=15, rss=24 (1-based)
    // We'll parse tokens
    vector<string> tok; tok.reserve(64);
    string cur; istringstream iss(rest); while(iss>>cur) tok.push_back(cur);
    if(tok.size()<24-2) return false; // because we started from field 3
    utime=stoull(tok[14-3]); stime=stoull(tok[15-3]); rss=stol(tok[24-3]);
    return true;
}

int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);

    cout<<fixed<<setprecision(1);

    enum SortMode { BY_CPU, BY_MEM }; SortMode sortMode=BY_CPU;

    unordered_map<int, ProcPrev> prev; // per-pid delta history
    CPUStat prevCPU{}; readCPU(prevCPU);

    const long pageKB = sysconf(_SC_PAGESIZE) / 1024;
    const int refresh_ms = 2000; // 2s

    while(true){
        // 1) Read global CPU+Mem
        CPUStat nowCPU; if(!readCPU(nowCPU)) { cerr<<"Cannot read /proc/stat\n"; return 1; }
        unsigned long long totDelta = totalJiff(nowCPU) - totalJiff(prevCPU);
        unsigned long long idleDelta = idleJiff(nowCPU) - idleJiff(prevCPU);
        double cpuTotalPct = totDelta ? (100.0 * (totDelta - idleDelta) / totDelta) : 0.0;
        MemInfo mi = readMem(); unsigned long long usedKB = mi.totalKB - mi.availKB;
        double memPct = mi.totalKB ? (100.0 * (double)usedKB / mi.totalKB) : 0.0;

        // 2) Build process table
        vector<ProcInfo> procs;
        for(int pid : listPids()){
            string name; unsigned long long ut=0, st=0; long rss=0;
            if(!readStatForPid(pid,name,ut,st,rss)) continue;
            unsigned long long procTotal = ut + st;
            double cpu=0.0;
            auto it = prev.find(pid);
            if(it!=prev.end() && totDelta>0){
                unsigned long long pdelta = procTotal - it->second.procTotal;
                cpu = 100.0 * (double)pdelta / (double)totDelta;
            }
            prev[pid].procTotal = procTotal;

            ProcInfo pi; pi.pid=pid; pi.name=name;
            pi.cpu = cpu;
            pi.rssKB = (unsigned long long) ( (rss<0?0:rss) * pageKB );
            pi.mem = mi.totalKB ? (100.0 * (double)pi.rssKB / mi.totalKB) : 0.0;
            procs.push_back(move(pi));
        }

        // 3) Sort
        if(sortMode==BY_CPU){
            sort(procs.begin(), procs.end(), [](const ProcInfo& a, const ProcInfo& b){
                if(fabs(a.cpu-b.cpu)>1e-6) return a.cpu>b.cpu;
                return a.pid<b.pid;
            });
        }else{
            sort(procs.begin(), procs.end(), [](const ProcInfo& a, const ProcInfo& b){
                if(fabs(a.mem-b.mem)>1e-6) return a.mem>b.mem;
                return a.pid<b.pid;
            });
        }

        // 4) Render
        cout<<"\033[2J\033[H"; // clear + home
        cout<<"System Monitor (mini-top)  |  CPU "<<cpuTotalPct<<"%  |  MEM "<<memPct<<"%  ("
            <<usedKB/1024<<"MiB/"<<mi.totalKB/1024<<"MiB used)  |  sort: "
            <<(sortMode==BY_CPU?"CPU":"MEM")<<"\n";
        cout<<"Cmds: [c]=sort CPU  [m]=sort MEM  [k <pid>]=kill  [q]=quit\n";
        cout<<left<<setw(8)<<"PID"<<setw(7)<<"CPU%"<<setw(7)<<"MEM%"<<setw(8)<<"RSS(MB)"<<"NAME\n";
        int shown=0; const int maxRows=30;
        for(const auto& p: procs){
            if(shown++>=maxRows) break;
            cout<<left<<setw(8)<<p.pid
                <<setw(7)<<p.cpu
                <<setw(7)<<p.mem
                <<setw(8)<< (p.rssKB/1024.0/1024.0*1024.0) // keep integer-ish; harmless
                <<p.name<<"\n";
        }
        cout.flush();

        // 5) Wait for input (non-blocking with timeout)
        fd_set rfds; FD_ZERO(&rfds); FD_SET(STDIN_FILENO,&rfds);
        struct timeval tv; tv.tv_sec = refresh_ms/1000; tv.tv_usec = (refresh_ms%1000)*1000;
        int rv = select(STDIN_FILENO+1, &rfds, nullptr, nullptr, &tv);

        if(rv>0 && FD_ISSET(STDIN_FILENO,&rfds)){
            string line; if(!getline(cin,line)) break;
            if(line=="q") break;
            else if(line=="c") sortMode=BY_CPU;
            else if(line=="m") sortMode=BY_MEM;
            else if(line.size()>0 && line[0]=='k'){
                istringstream iss(line); string k; int pid; iss>>k>>pid;
                if(pid>1){
                    if(kill(pid, SIGTERM)==0) { /* ok */ }
                    else perror("kill");
                }
            }
        }

        prevCPU = nowCPU;
    }

    cout<<"\nBye.\n";
    return 0;
}
