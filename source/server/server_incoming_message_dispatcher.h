#ifndef _INCOMING_MESSAGE_DISPATCHER_H_
#define _INCOMING_MESSAGE_DISPATCHER_H_

#include <cassert>
#include <string>
#include <memory>
#include <boost/format.hpp>

#include <order_matcher/incoming_message.h>
#include <order_matcher/central_order_book.h>

#include <concurrent/thread.h>
#include <concurrent/actor.h>
#include <concurrent/queue_mpsc.hpp>

#include <concurrent/profiling/thread_profiler.h>

#include <utility/logger/logger.h>

#include <server/server_constants.h>

using namespace concurrent;
using namespace order_matcher;


class IncomingMessageDispatcher : public Actor
{
    public:

        using IncomingMessageQueue = concurrent::QueueMPSC<IncomingMessage>;
        using IncomingMessageQueueNode = concurrent::QueueMPSC<IncomingMessage>::QueueMPSCNode;

        IncomingMessageDispatcher() : Actor("IncomingWorker"), m_centralOrderBook{nullptr}
        // We can`t have more than 16 characters in Linux for a pthread name ,that is why compacted the thread name...
        {
        }

        void setCentralOrderBook(order_matcher::CentralOrderBook* centralOrderBook)
        {
            assert(centralOrderBook != nullptr);
            m_centralOrderBook = centralOrderBook;
        }

        void pushMessage(const IncomingMessage& message)
        {
            m_queue.push(message);
        }

        void* run() override
        {
            DECLARE_THREAD_PROFILER;
            THREAD_PROFILER_START;

            LOG_INFO("Incoming message dispatcher", "Thread starting")
            // Let`s wait until central order book initialisation
            while (true)
            {
                if (isFinishing() == true)
                {
                    break;
                }

                if (m_centralOrderBook == nullptr)
                {
                    concurrent::Thread::sleep(server_constants::SERVER_THREAD_SLEEP_DURATION);
                }
                else
                {
                    break;
                }
            }

            while (true)
            {
                if (isFinishing() == true)
                {
                    break;
                }

                IncomingMessageQueueNode* messageLinkedList = nullptr;

                if ((messageLinkedList = m_queue.flush()) != nullptr)
                {
                    // Traverse linked list of incoming messages
                    while (messageLinkedList)
                    {
                        auto incomingMessage = messageLinkedList->m_data;

                        switch (incomingMessage.getType())
                        {
                            case IncomingMessageType::NEW_ORDER:
                                m_centralOrderBook->addOrder(incomingMessage.getOrder());
                            break;

                            case IncomingMessageType::CANCEL_ORDER:
                                m_centralOrderBook->cancelOrder(incomingMessage.getOrder(), incomingMessage.getOrigClientOrderID());
                            break;

                            default:
                            break;
                        }
                        std::unique_ptr< IncomingMessageQueueNode > temp{ messageLinkedList };
                        messageLinkedList = messageLinkedList->m_next;
                    }

                }
                else
                {
                    concurrent::Thread::yield();
                }
            }

            THREAD_PROFILER_END;
            THREAD_PROFILER_TRACE;

            LOG_INFO("Incoming message dispatcher", "Thread exiting")
            return nullptr;
        }

    private :
        order_matcher::CentralOrderBook* m_centralOrderBook;
        concurrent::QueueMPSC<IncomingMessage> m_queue;

};

#endif