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

class CombinedPriceList {
public:
    CombinedPriceList() = default;

    set<AProducer> producers;
    APriceList priceList;

    void Add(const AProducer &producer, const APriceList &_priceList) {
        // If producer haven't already added products to this priceList.
        if (producers.find(producer) != producers.end()) {
            for (auto &item : _priceList->m_List) {
                // If there is a duplicity in a product (by width and height).
                for (auto &addedItem : priceList->m_List) {
                    if ((addedItem.m_H == item.m_H && addedItem.m_W == item.m_W) ||
                        (addedItem.m_H == item.m_W && addedItem.m_W == item.m_H)) {
                        // What is the lower cost?
                        if (item.m_Cost < addedItem.m_Cost) {
                            addedItem.m_Cost = item.m_Cost;
                        }
                    } // Different product.
                    else {
                        priceList->Add(item);
                    }
                }
                priceList->Add(item);
            }
        }
    }
};

class CWeldingCompany {
private:
    vector<thread> threads;
    set<AProducer> producers;
    set<ACustomer> customers;
    map<unsigned int, CombinedPriceList> priceLists; // material: combined price list

    // TODO: buffery?

    void Process();

    void ProcessCustomer();

public:
    static void SeqSolve(APriceList priceList, COrder &order);

    void AddProducer(AProducer prod);

    void AddCustomer(ACustomer cust);

    void AddPriceList(AProducer prod, APriceList priceList);

    void Start(unsigned thrCount);

    void Stop();
};

void CWeldingCompany::SeqSolve(APriceList priceList, COrder &order) {
#ifdef MY_DEBUG
    cout << "SeqSolver" << endl;
#endif

    vector<COrder> orderList;
    orderList.emplace_back(order);
    ProgtestSolver(orderList, priceList);
}

void CWeldingCompany::AddProducer(AProducer prod) {
#ifdef MY_DEBUG
    cout << "Add producer " << prod << endl;
#endif

    // TODO: maybe some mutex?
    producers.insert(prod);
}

void CWeldingCompany::AddCustomer(ACustomer cust) {
#ifdef MY_DEBUG
    cout << "Add customer " << cust << endl;
#endif

    // TODO: maybe some mutex?
    customers.insert(cust);
    threads.emplace_back(&CWeldingCompany::ProcessCustomer, this);
}

void CWeldingCompany::AddPriceList(AProducer prod, APriceList priceList) {
#ifdef MY_DEBUG
    cout << "Add priceList " << priceList << " from producer " << prod << endl;
#endif

    // TODO: maybe some mutex?

    if (priceLists.find(priceList->m_MaterialID) != priceLists.end()) {
        CombinedPriceList cpl;
        cpl.Add(prod, priceList);
        priceLists.insert(make_pair(priceList->m_MaterialID, cpl));
    } else {
        priceLists[priceList->m_MaterialID].Add(prod, priceList);
    }
}

void CWeldingCompany::Start(unsigned thrCount) {
#ifdef MY_DEBUG
    cout << "Starting with " << thrCount << " threads" << endl;
#endif

    // Create threads.
    for (unsigned int i = 0; i < thrCount; i++) {
        threads.emplace_back(&CWeldingCompany::Process, this);
    }
}

void CWeldingCompany::Stop() {
#ifdef MY_DEBUG
    cout << "Stopping" << endl;
#endif
    // TODO: počkat na obsloužení všech zákazníků a na naceněné a vrácené poptávky – to dát do Process
    // TODO: notify všechny podmínky, aby se probraly

    // Wait for threads to finish.
    for (auto &thread : threads) {
        thread.join();
    }

#ifdef MY_DEBUG
    cout << "Stopped" << endl;
#endif
}

void CWeldingCompany::Process() {
#ifdef MY_DEBUG
    cout << "Starting Process thread" << endl;
#endif

    // TODO
}

void CWeldingCompany::ProcessCustomer() {
#ifdef MY_DEBUG
    cout << "Starting ProcessCustomer thread" << endl;
#endif

    // TODO
}

// TODO: CWeldingCompany implementation goes here

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
