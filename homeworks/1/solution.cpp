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
#include <unistd.h>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

// --------------------------------------------------------------------------------------

// Combined combinedPriceList by materialId from multiple producers.
class CombinedPriceList {
private:
    vector<APriceList> priceLists;
    APriceList combinedPriceList;
    bool combined = false;
    set<AProducer> producers;

    void CombinePriceLists();

public:

    explicit CombinedPriceList(unsigned _materialId) : combinedPriceList(make_shared<CPriceList>(_materialId)) {}

    void Add(AProducer producer, APriceList priceList);

    bool ContainsProducer(AProducer producer);

    APriceList GetCombinedPriceList();

    unsigned GetProducersSize();
};

void CombinedPriceList::CombinePriceLists() {
    for (auto &priceList : priceLists) {
        for (auto &newProduct: priceList->m_List) {
            bool duplicity = false;
            for (auto &product: combinedPriceList->m_List) {
                // The product has the same dimension, or is rotated by 90 degrees.
                if ((product.m_H == newProduct.m_H && product.m_W == newProduct.m_W) ||
                    (product.m_H == newProduct.m_W && product.m_W == newProduct.m_H)) {
                    duplicity = true;

                    // What does have the lower cost?
                    if (newProduct.m_Cost < product.m_Cost) {
                        product.m_Cost = newProduct.m_Cost;
                    }
                }
            }

            if (!duplicity) {
                combinedPriceList->Add(newProduct);
            }
        }
    }
}

void CombinedPriceList::Add(AProducer producer, APriceList priceList) {
    // If producer have already added products to this combinedPriceList, return.
    if (ContainsProducer(producer)) {
        return;
    }

    // Check the producer and add the price list.
    producers.insert(producer);
    priceLists.emplace_back(priceList);
}

bool CombinedPriceList::ContainsProducer(AProducer producer) {
    return producers.find(producer) != producers.end();
}

APriceList CombinedPriceList::GetCombinedPriceList() {
    if (!combined) {
        combined = true;
        CombinePriceLists();
    }

    return combinedPriceList;
}

unsigned CombinedPriceList::GetProducersSize() {
    return producers.size();
}

// --------------------------------------------------------------------------------------

// Pair with the customer and the order list.
class CombinedOrderList {
public:
    ACustomer customer;
    AOrderList orderList;

    CombinedOrderList(ACustomer customer, AOrderList orderList);
};

CombinedOrderList::CombinedOrderList(ACustomer customer, AOrderList orderList) : customer(customer),
                                                                                 orderList(orderList) {}

// --------------------------------------------------------------------------------------

class CWeldingCompany {
private:
    vector<thread> threads;
    vector<thread> customerThreads;
    set<AProducer> producers;
    set<ACustomer> customers;
    atomic<unsigned> activeCustomers;
    map<unsigned, CombinedPriceList> priceLists; // materialId: combined price list
    queue<CombinedOrderList> buffer;

    mutex bufferMutex;
    mutex priceListsMutex;

    condition_variable bufferEmptyCV;
    condition_variable priceListReadyCV;

    void ProcessBuffer();

    void ProcessCustomer(ACustomer customer);

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
    unique_lock<mutex> priceListGuard(priceListsMutex);

    // If the price list exists.
    if (priceLists.find(priceList->m_MaterialID) != priceLists.end()) {
        priceLists.at(priceList->m_MaterialID).Add(prod, priceList);
    } else {
        CombinedPriceList cpl = CombinedPriceList(priceList->m_MaterialID);
        cpl.Add(prod, priceList);
        priceLists.insert(make_pair(priceList->m_MaterialID, cpl));
    }
    priceListGuard.unlock();

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
                   (priceLists.at(bufferItem.orderList->m_MaterialID).GetProducersSize() == producers.size());
        });
        usleep(rand() % 100);
        APriceList priceList = priceLists.at(bufferItem.orderList->m_MaterialID).GetCombinedPriceList();
        priceListGuard.unlock();

        // Compute the costs.
        ProgtestSolver(bufferItem.orderList->m_List, priceList);

        // Flag this order list as completed.
        bufferItem.customer->Completed(bufferItem.orderList);
    }
}

void CWeldingCompany::ProcessCustomer(ACustomer customer) {
    AOrderList orderList;

    usleep(rand() % 100);

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
    srand(time(NULL));

    using namespace std::placeholders;

    CWeldingCompany test;

    AProducer p1 = make_shared<CProducerSync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    AProducerAsync p2 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
    test.AddProducer(p1);
    test.AddProducer(p2);
    test.AddCustomer(make_shared<CCustomerTest>(1));
    test.AddCustomer(make_shared<CCustomerTest>(2));
    p2->Start();
    test.Start(10);
    test.Stop();
    p2->Stop();

    return 0;

//    using namespace placeholders;
//    CWeldingCompany test;
//
//    AProducer p1 = make_shared<CProducerSync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    AProducerAsync p2 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    AProducerAsync p3 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    AProducerAsync p4 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    AProducerAsync p5 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    AProducerAsync p6 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    AProducerAsync p7 = make_shared<CProducerAsync>(bind(&CWeldingCompany::AddPriceList, &test, _1, _2));
//    test.AddProducer(p1);
//    test.AddProducer(p2);
//    test.AddProducer(p3);
//    test.AddProducer(p4);
//    test.AddProducer(p5);
//    test.AddProducer(p6);
//    test.AddProducer(p7);
//    //test . AddCustomer ( make_shared<CCustomerTest> ( 9850 ) );
//    //test . AddCustomer ( make_shared<CCustomerTest> ( 8660 ) );
//    //test . AddCustomer ( make_shared<CCustomerTest> ( 6610 ) );
//    //test . AddCustomer ( make_shared<CCustomerTest> ( 8530 ) );
//
//    test.AddCustomer(make_shared<CCustomerTest>(39400));
//    test.AddCustomer(make_shared<CCustomerTest>(34640));
////    test.AddCustomer(make_shared<CCustomerTest>(26440));
////    test.AddCustomer(make_shared<CCustomerTest>(34120));
//    p2->Start();
//    p3->Start();
//    p4->Start();
//    p5->Start();
//    p6->Start();
//    p7->Start();
//    test.Start(10);
//    test.Stop();
//    p2->Stop();
//    p3->Stop();
//    p4->Stop();
//    p5->Stop();
//    p6->Stop();
//    p7->Stop();
//
//    return 0;
}

#endif /* __PROGTEST__ */
