#include "/home/sancheetb/decs_project/cpp-httplib-master/httplib.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <string>

#define SERVER_CACHE_SIZE 50

const std::string SERVER_HOST = "localhost";
const int SERVER_PORT = 8080;
const int KEY_RANGE = 200;
const int POPULAR_RANGE = (1.0*SERVER_CACHE_SIZE)*1.25;

// Simple struct to hold results for one thread
struct ThreadStats {
    long long request_count = 0;
    double total_latency_ms = 0;
    long long errors = 0;
};

// Global flag to tell threads when to stop
std::atomic<bool> keep_running{true};

// The function each thread will run
void worker_thread(int id, std::string mode, ThreadStats& stats) {
    httplib::Client cli(SERVER_HOST, SERVER_PORT);
    cli.set_connection_timeout(5, 0); // 5 second timeout

    // Setup random number generator (Thread-safe way)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, KEY_RANGE - 1);
    std::uniform_int_distribution<> coin(0,1);
    std::uniform_int_distribution<> popular(0,POPULAR_RANGE-1);

    while (keep_running) {
        // Pick a random key (e.g., "key_42")
        std::string key = "key_" + std::to_string(dist(gen));
        std::string val = "value_" + std::to_string(dist(gen));

        // Start Timer
        auto start = std::chrono::high_resolution_clock::now();

        httplib::Result res;

        // --- SIMPLIFIED WORKLOAD LOGIC ---
        if (mode == "read") {
	    std::string request = "/read?key=" + key;
            res = cli.Get(request);
        }
        else if (mode == "create"){
	    std::string request = "/create?key=" + key + "&val=" + val;
            res = cli.Post(request);
        }
	else if (mode == "put_all"){
		std::string request;
		int decision = coin(gen);
		//std::cout << decision << '\n';
	    if(decision == 1){
		// send delete request
		request = "/delete?key=" + key;
		res = cli.Delete(request);
	    }
	    else{
		// send create request
		request = "/create?key=" + key + "&val=" + val;
		res = cli.Post(request);
	    }
	}
	else if (mode == "get_pop"){
		// send get requests for a small subset of database which is 1.25 times server cache size
		key = "key_" + std::to_string(popular(gen));
		std::string request = "/read?key=" + key;
		res = cli.Get(request);
	}

        // Stop Timer
        auto end = std::chrono::high_resolution_clock::now();

        // Check success
        if (res && res->status == 200) {
            stats.request_count++;
            // Calculate time taken in milliseconds
            std::chrono::duration<double, std::milli> elapsed = end - start;
            stats.total_latency_ms += elapsed.count();
        } else {
            stats.errors++;
        }
    }
}

int main(int argc, char* argv[]) {
    // Check command line arguments
    if (argc != 4) {
        std::cout << "Incorrect no. of arguments"<< std::endl;
        std::cout << "./load_gen <threads> <seconds> <read/create/put_all/get_pop>" << std::endl;
        return 1;
    }

    int num_threads = std::stoi(argv[1]);
    int duration_sec = std::stoi(argv[2]);
    std::string mode = argv[3];

    std::cout << "Workload: " << mode << " Threads: " << num_threads
              << " Duration: " << duration_sec << " sec" << std::endl;

    std::vector<std::thread> threads;
    std::vector<ThreadStats> all_stats(num_threads);

    // 1. Create and start all threads
    for (int i = 0; i < num_threads; ++i) {
        // Pass the stats object by reference using std::ref()
        threads.emplace_back(worker_thread, i, mode, std::ref(all_stats[i]));
    }

    // 2. Main thread waits for the specified duration
    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));

    // 3. Tell threads to stop
    keep_running = false;

    // 4. Wait for threads to finish their last request
    for (auto& t : threads) {
        t.join();
    }

    // 5. Calculate totals
    long long total_reqs = 0;
    double total_latency = 0;
    long long total_errors = 0;

    for (const auto& s : all_stats) {
        total_reqs += s.request_count;
        total_latency += s.total_latency_ms;
        total_errors += s.errors;
    }

    double avg_throughput = (double)total_reqs / duration_sec;
    double avg_latency = (total_reqs > 0) ? (total_latency / total_reqs) : 0.0;

    std::cout << "\nTotal Requests: " << total_reqs << std::endl;
    std::cout << "Total Errors:   " << total_errors << std::endl;
    std::cout << "Throughput:     " << avg_throughput << " req/sec" << std::endl;
    std::cout << "Avg Latency:    " << avg_latency << " ms" << std::endl;

    return 0;
}
