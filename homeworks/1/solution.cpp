#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <vector>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

// --------------------------------------------------------------------------------------

// Combined priceList by materialId from multiple producers.
class CombinedPriceList {
public:
    APriceList priceList;
    set<AProducer> producers;
    unsigned materialId;

    explicit CombinedPriceList(unsigned _materialId) : priceList(make_shared<CPriceList>(_materialId)),
                                                       materialId(_materialId) {}

    void Add(const AProducer &producer, const APriceList &_priceList);

    bool ContainsProducer(const AProducer &producer);
};

void CombinedPriceList::Add(const AProducer &producer, const APriceList &_priceList) {
#ifdef MY_DEBUG
    cout << "Add " << _priceList << " to priced list " << this << "." << endl;
#endif

    // If producer haven't already added products to this priceList.
    if (!ContainsProducer(producer)) {
        producers.insert(producer);

        for (auto &newItem : _priceList->m_List) {
            // If there is a duplicity in a product (by width and height).
            for (auto &item : priceList->m_List) {
                // The product has the same dimension, or is rotated by 90 degrees.
                if ((item.m_H == newItem.m_H && item.m_W == newItem.m_W) ||
                    (item.m_H == newItem.m_W && item.m_W == newItem.m_H)) {
                    // What does have the lower cost?
                    if (newItem.m_Cost < item.m_Cost) {
                        item.m_Cost = newItem.m_Cost;
                    }
                } // It is a different product.
                else {
                    priceList->Add(newItem);
                }
            }
        }
    }
}

bool CombinedPriceList::ContainsProducer(const AProducer &producer) {
    return producers.find(producer) != producers.end();
}

// --------------------------------------------------------------------------------------

class CombinedOrderList {
public:
    ACustomer customer;
    AOrderList orderList;

    CombinedOrderList(const ACustomer &customer, const AOrderList &orderList);
};

CombinedOrderList::CombinedOrderList(const ACustomer &customer, const AOrderList &orderList) : customer(customer),
                                                                                               orderList(orderList) {}

// --------------------------------------------------------------------------------------

class CWeldingCompany {
private:
    vector<thread> threads;
    set<AProducer> producers;
    set<ACustomer> customers;
    unsigned activeCustomers = 0;
    map<unsigned, CombinedPriceList> priceLists; // materialId: combined price list
    vector<CombinedOrderList> buffer;

    mutex bufferMutex;
    mutex priceListsMutex;
    mutex consoleMutex; // for debug purpose

    condition_variable bufferEmptyCV;
    condition_variable priceListReadyCV;
    // TODO: maybe add condition_variable customersCV;

    void ProcessBuffer();

    void ProcessCustomer(const ACustomer &customer);

public:
    static void SeqSolve(APriceList priceList, COrder &order);

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();
};

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
    vector<COrder> orderList;
    orderList.emplace_back(order);
    ProgtestSolver(orderList, priceList);
}

void CWeldingCompany::AddProducer(AProducer prod) {
#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Add producer " << prod << "." << endl;
    consoleGuard.unlock();
#endif

    producers.insert(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Add customer " << cust << "." << endl;
    consoleGuard.unlock();
#endif

    customers.insert(cust);
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Add priceList " << priceList << " from producer " << prod << "." << endl;
    consoleGuard.unlock();
#endif

    unique_lock<mutex> priceListGuard(priceListsMutex);

    // If the price list exists.
    if (priceLists.find(priceList->m_MaterialID) != priceLists.end()) {
        priceLists.at(priceList->m_MaterialID).Add(prod, priceList);
    } else {
        CombinedPriceList cpl = CombinedPriceList(priceList->m_MaterialID);
        cpl.Add(prod, priceList);
        priceLists.insert(make_pair(priceList->m_MaterialID, cpl));
    }

    // Wake up the threads waiting for the price list data.
    // TODO: is this ok?
    priceListReadyCV.notify_all();
}

void CWeldingCompany::Start(unsigned thrCount) {
    activeCustomers = customers.size();

#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Starting with " << thrCount << " threads and " << activeCustomers << " customers." << endl;
    consoleGuard.unlock();
#endif

    // Create threads buffer processing.
    for (unsigned i = 0; i < thrCount; i++) {
        threads.emplace_back(&CWeldingCompany::ProcessBuffer, this);
    }

    // Create threads for customer processing.
    for (auto &customer : customers) {
        threads.emplace_back(&CWeldingCompany::ProcessCustomer, this, customer);
    }
}

void CWeldingCompany::Stop() {
#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Stopping." << endl;
    consoleGuard.unlock();
#endif
    bufferEmptyCV.notify_all();
    priceListReadyCV.notify_all();

    // Wait for the threads to finish.
    for (auto &thread : threads) {
#ifdef MY_DEBUG
        consoleGuard.lock();
        cout << "Thread is joinable " << thread.joinable() << "." << endl;
        consoleGuard.unlock();
#endif
        thread.join();
    }

#ifdef MY_DEBUG
    consoleGuard.lock();
    cout << "Stopped." << endl;
#endif
}

void CWeldingCompany::ProcessBuffer() {
#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Starting processing the buffer thread" << endl;
    consoleGuard.unlock();
#endif

    while (true) {
        unique_lock<mutex> bufferGuard(bufferMutex);
        // If there are no data and no active customers, break.
        if (buffer.empty() && activeCustomers == 0) {
            break;
        }

        // Wait for the buffer data or customers to load the buffer data.
        bufferEmptyCV.wait(bufferGuard, [this]() {
            return !buffer.empty() || activeCustomers == 0;
        });

        // If there are no data and no active customers, break.
        if (buffer.empty() && activeCustomers == 0) {
            break;
        }

#ifdef MY_DEBUG
        consoleGuard.lock();
        cout << "Get buffer item." << endl;
        consoleGuard.unlock();
#endif

        // Get the order list from buffer.
        CombinedOrderList bufferItem = buffer.back();
        // TODO: maybe something like break if bufferItem == buffer.end() ?
        buffer.pop_back();
        bufferGuard.unlock();

        unique_lock<mutex> priceListGuard(priceListsMutex);
        // Does the priceList exist and is completed?
        priceListReadyCV.wait(priceListGuard, [&]() {
            return (priceLists.find(bufferItem.orderList->m_MaterialID) != priceLists.end()) &&
                   (priceLists.at(bufferItem.orderList->m_MaterialID).producers.size() == producers.size());
        });
        // Compute the costs.
        ProgtestSolver(bufferItem.orderList->m_List, priceLists.at(bufferItem.orderList->m_MaterialID).priceList);
        priceListGuard.unlock();

#ifdef MY_DEBUG
        consoleGuard.lock();
        cout << "Completed order list " << bufferItem.orderList << endl;
        consoleGuard.unlock();
#endif

        // Flag this order list as completed.
        bufferItem.customer->Completed(bufferItem.orderList);
    }
}

void CWeldingCompany::ProcessCustomer(const ACustomer &customer) {
#ifdef MY_DEBUG
    unique_lock<mutex> consoleGuard(consoleMutex);
    cout << "Starting processing the customer thread " << customer << endl;
    consoleGuard.unlock();
#endif

    AOrderList orderList;
    // TODO: maybe orderList.get() != nullptr ?
    while ((orderList = customer->WaitForDemand()) != nullptr) {
        // Request price lists from producers.
        for (auto &producer : producers) {
            producer->SendPriceList(orderList->m_MaterialID);
        }

#ifdef MY_DEBUG
        consoleGuard.lock();
        cout << "Added orderList to the buffer " << orderList << endl;
        consoleGuard.unlock();
#endif

        // Send this orderList to the buffer.
        unique_lock<mutex> bufferGuard(bufferMutex);
        buffer.emplace_back(customer, orderList);
        // Wake up the threads waiting for the buffer data.
        // TODO: is this ok?
        bufferEmptyCV.notify_all();
    }

    // This customer is not active anymore.
    activeCustomers--;
}

//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main() {
    using namespace std::placeholders;
    CWeldingCompany test;

    AProducer p1 = make_shared<CProducerSync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    AProducerAsync p2 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    test.AddProducer(p1);
    test.AddProducer(p2);
    test.AddCustomer(make_shared<CCustomerTest>(2));
    p2->Start();
    test.Start(3);
    test.Stop();
    p2->Stop();
    return 0;
}

#endif /* __PROGTEST__ */
