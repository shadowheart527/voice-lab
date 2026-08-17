#ifndef MAIN_CONTEXT_LIVEFEED_H
#define MAIN_CONTEXT_LIVEFEED_H

#include <QObject>
#include <QTimer>
#include <QList>

class QWebSocketServer;
class QWebSocket;

namespace Main {

    class Config;
    class DataStore;

    // Streams the live tracked values (pitch, F1-F3, gender-read scores) as
    // JSON over a localhost WebSocket, so external visualizations -- the local
    // acousticgender.space genderspace plot in particular -- can render a
    // real-time dot without doing any signal analysis of their own.
    class LiveFeed : public QObject {
        Q_OBJECT

    public:
        LiveFeed(Config *config, DataStore *dataStore);
        ~LiveFeed() override;

    private:
        void tick();

        Config *mConfig;
        DataStore *mDataStore;
        QWebSocketServer *mServer = nullptr;
        QList<QWebSocket *> mClients;
        QTimer mTimer;
    };

}

#endif // MAIN_CONTEXT_LIVEFEED_H
