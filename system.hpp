#ifndef SYSTEM_HPP
#define SYSTEM_HPP

#include <exception>
#include <vector>
#include <list>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstddef>
#include <array>

#include "machine.hpp"

class FulfillmentFailure : public std::exception {};

class OrderNotReadyException : public std::exception {};

class BadOrderException : public std::exception {};

class BadPagerException : public std::exception {};

class OrderExpiredException : public std::exception {};

class RestaurantClosedException : public std::exception {};

struct WorkerReport {
    std::vector<std::vector<std::string>> collectedOrders;
    std::vector<std::vector<std::string>> abandonedOrders;
    std::vector<std::vector<std::string>> failedOrders;
    std::vector<std::string> failedProducts;
};

enum class State {
    IN_PROGRESS,
    DONE,
    ERROR,
    TIMEOUT
};

enum class Type {
    WORKER,
    MACHINE
};

struct Worker {
    unsigned int orderId;
    unsigned int nProductsWaiting;
    std::vector<std::string> names;
    std::vector<std::pair<std::shared_ptr<Machine>, std::unique_ptr<Product>>> products;
    WorkerReport report;
};

struct SharedStorage {
    SharedStorage(unsigned int numberOfWorkers, unsigned int numberOfMachines, unsigned int clientTimeout,
                  std::unordered_map<std::string, unsigned int> machineIds)
        : signalForOrder(10, true), signalForProducts(10, true), signalForWork(3, true),
        signalForClient(10, true), signalForReady(3, true),
        workers(numberOfWorkers), machineIds(machineIds),
        isMachineWorking(numberOfMachines, true),
        isOutOfOrder(numberOfMachines, false), states(numberOfWorkers),
        queuesToMachines(numberOfMachines), numberOfWorkers(numberOfWorkers),
        numberOfMachines(numberOfMachines), nWorkersWaiting(0), nMachinesWaiting(0),
        clientTimeout(clientTimeout), orderId(0), closed(false), closing(false) {
    }

    std::mutex mut;

    std::condition_variable forEnd;

    bool signalForEnd = true;

    std::array<std::condition_variable, 10> forOrder = {
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable()
    };

    std::vector<bool> signalForOrder;

    std::array<std::condition_variable, 10> forProducts = {
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable()
    };

    std::vector<bool> signalForProducts;

    std::array<std::condition_variable, 3> forWork = {
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable()
    };

    std::vector<bool> signalForWork;

    std::array<std::condition_variable, 10> forClient = {
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable()
    };

    std::vector<bool> signalForClient;

    std::array<std::condition_variable, 3> forReady = {
        std::condition_variable(),
        std::condition_variable(),
        std::condition_variable()
    };

    std::vector<bool> signalForReady;

    std::unordered_set<unsigned int> takenOrders;
    std::unordered_map<unsigned int, unsigned int> orderToWorker;
    std::vector<Worker> workers;
    std::unordered_map<std::string, unsigned int> machineIds;
    std::vector<bool> isMachineWorking;
    std::vector<bool>isOutOfOrder;
    std::vector<State> states;
    std::queue<unsigned int> machinesToTurnOn;
    std::vector<std::queue<int>> queuesToMachines;
    std::vector<unsigned int> pendingOrderIds;
    std::vector<std::vector<std::string>> pendingOrderDescs;
    std::queue<std::pair<unsigned int, std::vector<std::string>>> newOrders;

    // wszyscy klienci czekający (wywołali wait() lub wait(timeout) i są zawieszeni)
    std::unordered_set<unsigned int> waitingClients;

    // niecierpliwi klienci czekający (wywołali wait(timeout) i są zawieszeni)
    std::unordered_set<unsigned int> waitingClientsInHurry;

    unsigned int numberOfWorkers;
    unsigned int numberOfMachines;
    unsigned int nWorkersWaiting;
    unsigned int nMachinesWaiting;
    unsigned int clientTimeout;
    unsigned int orderId;
    unsigned int informatorId;
    Type informatorType;
    bool closed;
    bool closing;
};

class CoasterPager {
public:
    CoasterPager(std::shared_ptr<SharedStorage> s, unsigned int orderId)
        : orderId(orderId) {

        this->s = s;
    }

    void wait() const {
        {
            std::unique_lock<std::mutex> lock{s->mut};

            s->waitingClients.insert(orderId);
            s->signalForReady[orderId] = false;
            s->forReady[orderId].wait(lock, [this] { return s->signalForReady[orderId] == true; });

            s->waitingClients.erase(orderId);

            auto workerIter = s->orderToWorker.find(orderId);

            unsigned int workerId = workerIter->second;

            if (s->states[workerId] == State::ERROR) {
                lock.unlock();
                throw FulfillmentFailure();
            }
        }
    }

    void wait(unsigned int timeout) const {
        {
            std::unique_lock<std::mutex> lock{s->mut};

            s->waitingClients.insert(orderId);
            s->signalForReady[orderId] = false;
            s->forReady[orderId].wait_for(lock, std::chrono::milliseconds(timeout), [this] { return s->signalForReady[orderId] == true; });

            s->waitingClients.erase(orderId);

            auto workerIter = s->orderToWorker.find(orderId);

            if (workerIter != s->orderToWorker.end()) {
                unsigned int workerId = workerIter->second;

                if (s->states[workerId] == State::ERROR) {
                    lock.unlock();
                    throw FulfillmentFailure();
                }
            }
        }
    }

    [[nodiscard]] unsigned int getId() const {
        return orderId;
    }

    [[nodiscard]] bool isReady() const {
        bool res = false;

        {
            std::unique_lock<std::mutex> lock{s->mut};

            auto it = s->orderToWorker.find(orderId);

            if (it != s->orderToWorker.end() && s->states[it->second] == State::DONE) {
                res = true;
            }
        }

        return res;
    }
private:
    std::shared_ptr<SharedStorage> s;
    unsigned int orderId;
};

class System {
public:
    typedef std::unordered_map<std::string, std::shared_ptr<Machine>> machines_t;

    System(machines_t machines, unsigned int numberOfWorkers, unsigned int clientTimeout) {
        std::unordered_map<std::string, unsigned int> machineIds;

        int i = 0;

        for (auto it = machines.begin(); it != machines.end(); it++) {
            machineIds.insert(std::make_pair(it->first, i));
            i++;
        }

        s = std::make_shared<SharedStorage>(numberOfWorkers, machines.size(), clientTimeout, machineIds);

        for (unsigned int i = 0; i < numberOfWorkers; i++) {
            workersThrs.push_back(std::thread(&System::workerFun, this, i));
        }

        for (auto it = machineIds.begin(); it != machineIds.end(); it++) {
            machinesThrs.push_back(std::thread(&System::machineFun, this, it->second, it->first, machines.find(it->first)->second));
        }
    }

    std::vector<WorkerReport> shutdown() {
        std::vector<WorkerReport> reports;

        {
            std::unique_lock<std::mutex> lock{s->mut};
            std::cout << s->nWorkersWaiting << ' ' << s->nMachinesWaiting << std::endl;
            if (s->nWorkersWaiting == s->numberOfWorkers && s->nMachinesWaiting == s->numberOfMachines) {
                s->closing = true;
                s->closed = true;

                std::cout << "SHUTDOWN 1" << std::endl;

                for (size_t i = 0; i < workersThrs.size(); i++) {
                    s->signalForOrder[i] = true;
                    lock.unlock();
                    s->forOrder[i].notify_one();
                    lock.lock();
                }

                for (size_t i = 0; i < machinesThrs.size(); i++) {
                    s->signalForWork[i] = true;
                    lock.unlock();
                    s->forWork[i].notify_one();
                    lock.lock();
                }
            } else {
                s->closing = true;

                s->signalForEnd = false;
                s->forEnd.wait(lock, [this] { return s->signalForEnd == true; });

                std::cout << "SHUTDOWN 2" << std::endl;

                s->closed = true;

                for (size_t i = 0; i < workersThrs.size(); i++) {
                    if (!(s->informatorType == Type::WORKER && s->informatorId == i)) {
                        s->signalForOrder[i] = true;
                        lock.unlock();
                        s->forOrder[i].notify_one();
                        lock.lock();
                    }
                }

                for (size_t i = 0; i < machinesThrs.size(); i++) {
                    if (!(s->informatorType == Type::MACHINE && s->informatorId == i)) {
                        s->signalForWork[i] = true;
                        lock.unlock();
                        s->forWork[i].notify_one();
                        lock.lock();
                    }
                }
            }
        }

        for (size_t i = 0; i < workersThrs.size(); i++) {
            workersThrs[i].join();
        }

        for (size_t i = 0; i < machinesThrs.size(); i++) {
            machinesThrs[i].join();
        }

        for (size_t i = 0; i < workersThrs.size(); i++) {
            reports.push_back(s->workers[i].report);
        }

        return reports;
    }

    std::vector<std::string> getMenu() const {
        std::vector<std::string> menu;

        {
            std::unique_lock<std::mutex> lock{s->mut};

            if (!s->closing) {
                for (auto it = s->machineIds.begin(); it != s->machineIds.end(); it++) {
                    if (!s->isOutOfOrder[it->second]) {
                        menu.push_back(it->first);
                    }
                }
            }

            std::cout << "GET MENU BEGIN" << std::endl;
            std::cout << "  ";
            for (size_t i = 0; i < menu.size(); i++) {
                std::cout << menu[i] << ' ';
            }
            std::cout << std::endl;
            std::cout << "GET MENU END" << std::endl;

        }

        return menu;
    }

    std::vector<unsigned int> getPendingOrders() const {
        std::vector<unsigned int> pendingOrders;

        {
            std::unique_lock<std::mutex> lock{s->mut};

            pendingOrders = s->pendingOrderIds;

            std::cout << "PENDING ORDERS BEGIN" << std::endl;
            std::cout << "  ";
            for (size_t i = 0; i < s->pendingOrderIds.size(); i++) {
                std::cout << s->pendingOrderIds[i] << ' ';
            }
            std::cout << std::endl;
            std::cout << "PENDING ORDERS END" << std::endl;

        }

        return pendingOrders;
    }

    std::unique_ptr<CoasterPager> order(std::vector<std::string> products) {
        std::vector<std::string> menu;
        unsigned int orderId;

        {
            std::unique_lock<std::mutex> lock{s->mut};

            std::cout << "ORDER BEGIN" << std::endl; for (size_t i = 0; i < products.size(); i++) { std::cout << products[i] << ' '; } std::cout << std::endl;

            if (s->closing) {
                std::cout << "  RESTAURANT CLOSED" << std::endl;
                lock.unlock();
                throw RestaurantClosedException();
            }

            for (auto it = s->machineIds.begin(); it != s->machineIds.end(); it++) {
                if (!s->isOutOfOrder[it->second]) {
                    menu.push_back(it->first);
                }
            }

            std::cout << "  MENU" << std::endl; std::cout << "  "; for (size_t i = 0; i < menu.size(); i++) { std::cout << menu[i] << ' '; } std::cout << std::endl;

            for (std::string product : products) {
                if (std::find(menu.begin(), menu.end(), product) == menu.end()) {
                    std::cout << "  BAD ORDER" << std::endl;
                    lock.unlock();
                    throw BadOrderException();
                }
            }

            orderId = ++s->orderId;
            s->newOrders.push(std::make_pair(orderId, products));

            std::cout << "ORDER END" << std::endl;

            if (s->nWorkersWaiting > 0) {
                unsigned int i = 0;

                while (i < s->numberOfWorkers) {
                    if (s->signalForOrder[i] == false) {
                        std::cout << "BUDZONY: " << i << std::endl;

                        unsigned int orderId = s->newOrders.front().first;
                        std::vector<std::string> orderDesc = s->newOrders.front().second;

                        s->pendingOrderIds.push_back(orderId);
                        s->pendingOrderDescs.push_back(orderDesc);
                        s->workers[i].orderId = orderId;
                        s->states[i] = State::IN_PROGRESS;
                        s->orderToWorker[orderId] = i;
                        s->workers[i].names = orderDesc;
                        s->workers[i].nProductsWaiting = s->workers[i].names.size();
                        s->workers[i].products.clear();
                        s->newOrders.pop();

                        s->signalForOrder[i] = true;
                        lock.unlock();
                        s->forOrder[i].notify_one();
                        break;
                    }

                    i++;
                }
            }
        }

        return std::make_unique<CoasterPager>(s, orderId);
    }

    std::vector<std::unique_ptr<Product>> collectOrder(std::unique_ptr<CoasterPager> CoasterPager) {
        std::vector<std::unique_ptr<Product>> products;

        {
            std::unique_lock<std::mutex> lock{s->mut};

            std::cout << "COLLECT ORDER BEGIN" << std::endl;

            unsigned int orderId = CoasterPager->getId();
            unsigned int workerId = s->orderToWorker.find(orderId)->second;

            if (s->states[workerId] == State::ERROR) {
                std::cout << "  FULFILLMENT FAILURE" << std::endl;
                lock.unlock();
                throw FulfillmentFailure();
            }

            if (s->states[workerId] == State::IN_PROGRESS) {
                std::cout << "  ORDER NOT READY" << std::endl;
                lock.unlock();
                throw OrderNotReadyException();
            }

            if (s->states[workerId] == State::TIMEOUT) {
                std::cout << "  ORDER EXPIRED" << std::endl;
                lock.unlock();
                throw OrderExpiredException();
            }

            if (s->takenOrders.find(orderId) != s->takenOrders.end()) {
                std::cout << "  BAD PAGER" << std::endl;
                lock.unlock();
                throw BadPagerException();
            }

            s->takenOrders.insert(orderId);

            products.reserve(s->workers[workerId].products.size());

            for (unsigned int i = 0; i < s->workers[workerId].products.size(); i++) {
                products.emplace_back(std::move(s->workers[workerId].products[i].second));
            }

            std::cout << "COLLECT ORDER END" << std::endl;

            s->signalForClient[workerId] = true;
            lock.unlock();
            s->forClient[workerId].notify_one();
        }

        return products;
    }

    unsigned int getClientTimeout() const {
        unsigned int clientTimeout;

        {
            std::unique_lock<std::mutex> lock{s->mut};
            clientTimeout = s->clientTimeout;
        }

        return clientTimeout;
    }
private:
    void workerFun(int workerId) {
        while (true) {
            {
                std::unique_lock<std::mutex> lock{s->mut};
                std::cout << "WORKER " << workerId << " NEW ORDERS SIZE1 " << s->newOrders.size() << std::endl;
                // jeśli nie ma nowych zamówień pracownik jest zawieszany
                if (s->newOrders.size() == 0) {
                    std::cout << "WORKER " << workerId << " WAITS FOR ORDER" << std::endl;

                    s->nWorkersWaiting++;

                    if (s->closing && s->nWorkersWaiting == s->numberOfWorkers && s->nMachinesWaiting == s->numberOfMachines) {
                        s->informatorId = workerId;
                        s->informatorType = Type::WORKER;
                        std::cout << "WORKER " << workerId << " INFORMATOR" << std::endl;
                        s->signalForEnd = true;
                        lock.unlock();
                        s->forEnd.notify_one();
                        return;
                    }

                    s->signalForOrder[workerId] = false;
                    s->forOrder[workerId].wait(lock, [this, workerId] { return s->signalForOrder[workerId] == true; });

                    if (s->closed) {
                        std::cout << "WORKER " << workerId << " ENDED" << std::endl;
                        lock.unlock();
                        return;
                    }

                    // pracownik obudził się
                    s->nWorkersWaiting--;
                } else {
                    unsigned int orderId = s->newOrders.front().first;
                    std::vector<std::string> orderDesc = s->newOrders.front().second;

                    s->pendingOrderIds.push_back(orderId);
                    s->pendingOrderDescs.push_back(orderDesc);
                    s->workers[workerId].orderId = orderId;
                    s->states[workerId] = State::IN_PROGRESS;
                    s->orderToWorker[orderId] = workerId;
                    s->workers[workerId].names = orderDesc;
                    s->workers[workerId].nProductsWaiting = s->workers[workerId].names.size();
                    s->workers[workerId].products.clear();
                    s->newOrders.pop();
                }

                std::cout << "WORKER " << workerId << " NEW ORDERS SIZE2 " << s->newOrders.size() << std::endl;

                std::cout << "WORKER " << workerId << " GOT ORDER" << std::endl; for (size_t i = 0; i < s->workers[workerId].names.size(); i++) { std::cout << s->workers[workerId].names[i] << ' '; } std::cout << std::endl;

                std::unordered_set<unsigned int> distinctMachineIds;

                // ustawienie się pracownika w kolejkach do maszyn
                for (std::string name : s->workers[workerId].names) {
                    unsigned int machineId = s->machineIds.find(name)->second;
                    s->queuesToMachines[machineId].push(workerId);
                    distinctMachineIds.insert(machineId);
                }

                for (unsigned int machineId : distinctMachineIds) {
                    if (!s->isMachineWorking[machineId]) {
                        s->machinesToTurnOn.push(machineId);
                    }
                }

                std::cout << "WORKER " << workerId << " WAITS FOR PRODUCTS" << std::endl;

                // potokowe włączanie uśpionych maszyn
                if (s->machinesToTurnOn.size() > 0) {
                    int nextMachineId = s->machinesToTurnOn.front();
                    s->machinesToTurnOn.pop();
                    s->signalForWork[nextMachineId] = true;
                    lock.unlock();
                    s->forWork[nextMachineId].notify_one();
                }
            }

            {
                std::unique_lock<std::mutex> lock{s->mut};

                // zawieszenie pracownika w oczekiwaniu na wszystkie produkty
                s->signalForProducts[workerId] = false;
                s->forProducts[workerId].wait(lock, [this, workerId] { return s->signalForProducts[workerId] == true; });

                if (s->states[workerId] != State::ERROR) {
                    s->states[workerId] = State::DONE;

                    std::cout << "WORKER " << workerId << " GOT PRODUCTS SUCCESSFULLY" << std::endl;
                } else {
                    std::cout << "WORKER " << workerId << " DID NOT GET PRODUCTS" << std::endl;
                }

                s->signalForReady[s->workers[workerId].orderId] = true;
                lock.unlock();
                s->forReady[s->workers[workerId].orderId].notify_one();
            }

            {
                std::unique_lock<std::mutex> lock{s->mut};

                // zamówienie zostało już przyjęte, więc można je usunąć z listy
                if (s->pendingOrderIds.size() > 0) {
                    auto orderIdIter = s->pendingOrderIds.begin();
                    auto orderDescIter = s->pendingOrderDescs.begin();

                    while (orderIdIter != s->pendingOrderIds.end()) {
                        if (*orderIdIter == s->workers[workerId].orderId) {
                            break;
                        }

                        orderIdIter++;
                        orderDescIter++;
                    }

                    if (orderIdIter != s->pendingOrderIds.end()) {
                        s->pendingOrderIds.erase(orderIdIter);
                        s->pendingOrderDescs.erase(orderDescIter);
                    }
                }

                if (s->states[workerId] == State::DONE) {
                    std::cout << "WORKER " << workerId << " WAITS ON CLIENT" << std::endl;

                    // czekanie ileś czasu na odebranie przez klienta
                    s->signalForClient[workerId] = false;

                    if (s->forClient[workerId].wait_for(lock, std::chrono::milliseconds(s->clientTimeout), [this, workerId] { return s->signalForClient[workerId] == true; })) {
                        std::cout << "WORKER " << workerId << " CLIENT CAME" << std::endl;
                        s->workers[workerId].report.collectedOrders.push_back(s->workers[workerId].names);
                    } else {
                        std::cout << "WORKER " << workerId << " CLIENT DID NOT COME" << std::endl;
                        s->states[workerId] = State::TIMEOUT;

                        // zwrot
                        for (size_t i = 0; i < s->workers[workerId].products.size(); i++) {
                            s->workers[workerId].products[i].first->returnProduct(std::move(s->workers[workerId].products[i].second));
                        }

                        s->workers[workerId].report.abandonedOrders.push_back(s->workers[workerId].names);
                    }
                } else {
                    std::cout << "WORKER " << workerId << " RETURNS PRODUCTS BECAUSE OF MACHINE(S) ERROR(S)" << std::endl;
                    // zwrot
                    for (size_t i = 0; i < s->workers[workerId].products.size(); i++) {
                        s->workers[workerId].products[i].first->returnProduct(std::move(s->workers[workerId].products[i].second));
                    }

                    s->workers[workerId].report.failedOrders.push_back(s->workers[workerId].names);
                }
            }
        }
    }

    void machineFun(unsigned int machineId, std::string name, std::shared_ptr<Machine> machine) {
        int workerId;

        machine->start();

        while (true) {
            {
                std::unique_lock<std::mutex> lock{s->mut};

                if (s->queuesToMachines[machineId].size() == 0) {
                    std::cout << "MACHINE " << name << " WAITS FOR TURN ON" << std::endl;
                    s->isMachineWorking[machineId] = false;
                    s->nMachinesWaiting++;

                    if (s->closing && s->nWorkersWaiting == s->numberOfWorkers && s->nMachinesWaiting == s->numberOfMachines) {
                        s->informatorId = machineId;
                        s->informatorType = Type::MACHINE;
                        s->signalForEnd = true;
                        lock.unlock();
                        s->forEnd.notify_one();
                        return;
                    }

                    s->signalForWork[machineId] = false;
                    s->forWork[machineId].wait(lock, [this, machineId] { return s->signalForWork[machineId] == true; });

                    if (s->closed) {
                        std::cout << "MACHINE " << name << " ENDED" << std::endl;
                        machine->stop();
                        lock.unlock();
                        return;
                    }

                    std::cout << "MACHINE " << name << " WAS TURNED ON" << std::endl;
                    s->isMachineWorking[machineId] = true;
                    s->nMachinesWaiting--;
                    workerId = s->queuesToMachines[machineId].front();
                    std::cout << "MACHINE " << name << " WORK FOR WORKER " << workerId << std::endl;
                    s->queuesToMachines[machineId].pop();

                    // potokowe włączanie uśpionych maszyn
                    if (s->machinesToTurnOn.size() > 0) {
                        int nextMachineId = s->machinesToTurnOn.front();
                        s->machinesToTurnOn.pop();
                        s->signalForWork[nextMachineId] = true;
                        lock.unlock();
                        s->forWork[nextMachineId].notify_one();
                    }
                } else {
                    workerId = s->queuesToMachines[machineId].front();
                    std::cout << "MACHINE " << name << " WORK FOR WORKER " << workerId << std::endl;
                    s->queuesToMachines[machineId].pop();
                }
            }

            bool produced = true;
            std::unique_ptr<Product> product;

            try {
                product = machine->getProduct();
            } catch(MachineFailure& e) {
                produced = false;
            }

            {
                std::unique_lock<std::mutex> lock{s->mut};

                if (produced) {
                    s->workers[workerId].products.emplace_back(std::make_pair(machine, std::move(product)));
                } else {
                    s->isOutOfOrder[machineId] = true;
                    s->states[workerId] = State::ERROR;
                    s->workers[workerId].report.failedProducts.push_back(name);

                    // maszyna się zepsuła, więc należy usunąć wszystkie zamówienia, w których występuje nazwa produktu maszyny
                    std::vector<unsigned int> pendingOrderIds;
                    std::vector<std::vector<std::string>> pendingOrderDescs;

                    for (size_t i = 0; i < s->pendingOrderDescs.size(); i++) {
                        if (std::find(s->pendingOrderDescs[i].begin(), s->pendingOrderDescs[i].end(), name) == s->pendingOrderDescs[i].end()) {
                            pendingOrderIds.push_back(s->pendingOrderIds[i]);
                            pendingOrderDescs.push_back(s->pendingOrderDescs[i]);
                        }
                    }

                    s->pendingOrderIds = pendingOrderIds;
                    s->pendingOrderDescs = pendingOrderDescs;
                }

                std::cout << "MACHINE " << name << " PRODUCED (" << produced << ") FOR WORKER " << workerId << std::endl;

                s->workers[workerId].nProductsWaiting--;

                if (s->workers[workerId].nProductsWaiting == 0) {
                    std::cout << "MACHINE " << name << " WAKES WORKER " << workerId << std::endl;
                    s->signalForProducts[workerId] = true;
                    lock.unlock();
                    s->forProducts[workerId].notify_one();
                }
            }
        }
    }

    std::shared_ptr<SharedStorage> s;
    std::vector<std::thread> workersThrs;
    std::vector<std::thread> machinesThrs;
};

#endif // SYSTEM_HPP
