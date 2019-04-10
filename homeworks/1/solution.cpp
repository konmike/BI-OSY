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

// Combined combinedPriceList by materialId from multiple producers.
class CombinedPriceList {
public:
    APriceList combinedPriceList;
    set<AProducer> producers;
    unsigned materialId;

    explicit CombinedPriceList(unsigned _materialId) : combinedPriceList(make_shared<CPriceList>(_materialId)),
                                                       materialId(_materialId) {}

    void Add(const AProducer &producer, const APriceList &_priceList);

    bool ContainsProducer(const AProducer &producer);
};

void CombinedPriceList::Add(const AProducer &producer, const APriceList &_priceList) {
    // If producer have already added products to this combinedPriceList, return.
    if (ContainsProducer(producer)) {
        return;
    }

    producers.insert(producer);

    long originalSize = combinedPriceList->m_List.size();

    for (auto &newItem : _priceList->m_List) {
        if (originalSize == 0) {
            combinedPriceList->Add(newItem);
            continue;
        }

        if (combinedPriceList->m_List.empty()) {
            combinedPriceList->Add(newItem);
        } else {
            // If there is a duplicity in a product (by width and height).
            for (int i = 0; i < originalSize; i++) {
                CProd &prod = combinedPriceList->m_List[i];

                // The product has the same dimension, or is rotated by 90 degrees.
                if ((prod.m_H == newItem.m_H && prod.m_W == newItem.m_W) ||
                    (prod.m_H == newItem.m_W && prod.m_W == newItem.m_H)) {
                    // What does have the lower cost?
                    if (newItem.m_Cost < prod.m_Cost) {
                        prod.m_Cost = newItem.m_Cost;
                    }
                } // It is a different product.
                else {
                    combinedPriceList->Add(newItem);
                }
            }
        }
    }
}

bool CombinedPriceList::ContainsProducer(const AProducer &producer) {
    return producers.find(producer) != producers.end();
}

// --------------------------------------------------------------------------------------

// Pair with the customer and the order list.
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
    vector<thread> customerThreads;
    set<AProducer> producers;
    set<ACustomer> customers;
    unsigned activeCustomers = 0;
    map<unsigned, CombinedPriceList> priceLists; // materialId: combined price list
    queue<CombinedOrderList> buffer;

    mutex bufferMutex;
    mutex priceListsMutex;

    condition_variable bufferEmptyCV;
    condition_variable priceListReadyCV;

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
    order = orderList.back();
}

void CWeldingCompany::AddProducer(AProducer prod) {
    producers.insert(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
    customers.insert(cust);
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
    // TODO: speed up
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
    priceListReadyCV.notify_all();
}

void CWeldingCompany::Start(unsigned thrCount) {
    activeCustomers = customers.size();

    // Create threads buffer processing.
    for (unsigned i = 0; i < thrCount; i++) {
        threads.emplace_back(&CWeldingCompany::ProcessBuffer, this);
    }

    // Create threads for customer processing.
    for (auto &customer : customers) {
        customerThreads.emplace_back(&CWeldingCompany::ProcessCustomer, this, customer);
    }
}

void CWeldingCompany::Stop() {
    // Wait for the customer threads to finish.
    for (auto &thread : customerThreads) {
        thread.join();
    }

    bufferEmptyCV.notify_all();

    // Wait for the threads to finish.
    for (auto &thread : threads) {
        thread.join();
    }
}

void CWeldingCompany::ProcessBuffer() {
    while (true) {
        unique_lock<mutex> bufferGuard(bufferMutex);
        // Wait for the buffer data or customers to load the buffer data.
        bufferEmptyCV.wait(bufferGuard, [this]() {
            return !buffer.empty() || activeCustomers == 0;
        });

        // If there are no data and no active customers, break.
        if (buffer.empty() && activeCustomers == 0) {
            break;
        }

        // Get the order list from buffer.
        CombinedOrderList bufferItem = buffer.front();
        buffer.pop();
        bufferGuard.unlock();

        unique_lock<mutex> priceListGuard(priceListsMutex);
        // Does the combinedPriceList exist and is completed?
        priceListReadyCV.wait(priceListGuard, [&]() {
            return (priceLists.find(bufferItem.orderList->m_MaterialID) != priceLists.end()) &&
                   (priceLists.at(bufferItem.orderList->m_MaterialID).producers.size() == producers.size());
        });
        APriceList priceList = priceLists.at(bufferItem.orderList->m_MaterialID).combinedPriceList;
        priceListGuard.unlock();

        // Compute the costs.
        ProgtestSolver(bufferItem.orderList->m_List, priceList);

        // Flag this order list as completed.
        bufferItem.customer->Completed(bufferItem.orderList);
    }
}

void CWeldingCompany::ProcessCustomer(const ACustomer &customer) {
    AOrderList orderList;
    while ((orderList = customer->WaitForDemand()) != nullptr) {
        // Request price lists from producers.
        for (auto &producer : producers) {
            producer->SendPriceList(orderList->m_MaterialID);
        }

        // Send this orderList to the buffer.
        unique_lock<mutex> bufferGuard(bufferMutex);
        buffer.emplace(customer, orderList);
        bufferGuard.unlock();

        // Wake up the threads waiting for the buffer data.
        bufferEmptyCV.notify_one();
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
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    p2->Start();
    test.Start(10);
    test.Stop();
    p2->Stop();

    return 0;
}

#endif /* __PROGTEST__ */
