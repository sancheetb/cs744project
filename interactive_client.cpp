#include <iostream>

#include "/home/sancheetb/decs_project/cpp-httplib-master/httplib.h"

void test(httplib::Client& cli) {
  if (auto res = cli.Post("/test")) {
    std::cout << "POST /test:" << res->status << '\n';
    std::cout << res->body << std::endl;
  } else {
    std::cerr << "POST request failed\n";
  }

  return;
}

void create(httplib::Client& cli, std::string key, std::string val) {
  std::string request = "/create?key=" + key + "&val=" + val;
  if (auto res = cli.Post(request)) {
    std::cout << "POST " << request << ":" << res->status << '\n';
    std::cout << res->body << '\n';
  } else {
    std::cerr << "POST request failed\n";
  }

  return;
}

void read(httplib::Client& cli, std::string key) {
  std::string request = "/read?key=" + key;

  if (auto res = cli.Get(request)) {
    std::cout << "READ " << request << ":" << res->status << '\n';
    std::cout << res->body << '\n';
  } else {
    std::cerr << "READ request failed\n";
  }

  return;
}

void deleteKey(httplib::Client& cli, std::string key) {
  std::string request = "/delete?key=" + key;

  if (auto res = cli.Delete(request)) {
    std::cout << "DELETE " << request << ":" << res->status << '\n';
    std::cout << res->body << '\n';
  } else {
    std::cerr << "DELETE request failed\n";
  }

  return;
}

void update(httplib::Client& cli, std::string key, std::string val) {
  std::string request = "/update?key=" + key + "&val=" + val;
  if (auto res = cli.Put(request)) {
    std::cout << "PUT " << request << ":" << res->status << '\n';
    std::cout << res->body << '\n';
  } else {
    std::cerr << "PUT request failed\n";
  }

  return;
}

int main() {
  httplib::Client cli("0.0.0.0", 8080);

  std::string key, value;
  int command;

  while(1){
	std::cin >> command;

	  if (command == 1){
		  // create request
		  std::cin >> key >> value;
		  create(cli, key, value);
	  }
	  else if (command == 2){
		  // read request
		  std::cin >> key;
		  read(cli, key);
	  }
	  else if (command == 3){
		  // update request
		  std::cin >> key >> value;
		  update(cli, key, value);
	  }
	  else if (command == 4){
		  // delete request
		  std::cin >> key;
		  deleteKey(cli, key);
	  }
	  else if(command == 5){
		  // exit client
		  std::cout << "Exiting.\n";
		  break;
	  }
	  else{
		  std::cout << "Enter valid command.\n";
	  }
  }

  return 0;
}

