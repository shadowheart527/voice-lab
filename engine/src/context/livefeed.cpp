#include "livefeed.h"
#include "config.h"
#include "datastore.h"
#include "genderscore.h"

#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <iostream>

using namespace Main;

// Median of the defined values of an optional track in [t0, t1], or `sentinel`
// when there are none (a sentinel, never NaN: -ffast-math folds isnan guards
// away). The default suits frequency tracks; the weight/tilt track needs a
// distinct one because -1.0 dB/oct is a legitimate tilt value.
static double medianDefinedIn(OptionalTimeTrack<double>& track, double t0, double t1,
        double sentinel = -1.0)
{
    rpm::vector<double> vals;
    for (auto it = track.lower_bound(t0); it != track.upper_bound(t1); ++it) {
        if (it->second.has_value()) {
            vals.push_back(*(it->second));
        }
    }
    if (vals.empty()) {
        return sentinel;
    }
    std::nth_element(vals.begin(), std::next(vals.begin(), (int) vals.size() / 2), vals.end());
    return vals[vals.size() / 2];
}

LiveFeed::LiveFeed(Config *config, DataStore *dataStore)
    : mConfig(config),
      mDataStore(dataStore)
{
    if (!config->getFeedEnabled()) {
        return;
    }

    const int port = config->getFeedPort();

    mServer = new QWebSocketServer(
            QStringLiteral("InFormant live feed"),
            QWebSocketServer::NonSecureMode, this);

    if (!mServer->listen(QHostAddress::LocalHost, port)) {
        std::cout << "LiveFeed] Could not listen on 127.0.0.1:" << port
                  << " (" << mServer->errorString().toStdString()
                  << "); live feed disabled" << std::endl;
        return;
    }

    std::cout << "LiveFeed] Serving live values on ws://127.0.0.1:" << port << std::endl;

    QObject::connect(mServer, &QWebSocketServer::newConnection, this, [this] {
        while (QWebSocket *sock = mServer->nextPendingConnection()) {
            mClients.append(sock);
            QObject::connect(sock, &QWebSocket::disconnected, this, [this, sock] {
                mClients.removeAll(sock);
                sock->deleteLater();
            });
        }
    });

    QObject::connect(&mTimer, &QTimer::timeout, this, [this] { tick(); });
    mTimer.start(50);
}

LiveFeed::~LiveFeed()
{
    for (QWebSocket *sock : mClients) {
        sock->close();
    }
}

void LiveFeed::tick()
{
    if (mClients.isEmpty()) {
        return;
    }

    // Same windows as the on-screen HUD: displayed pitch is a 0.35 s median,
    // formants 1.0 s, and the gender read runs on steadier 2.0 s medians.
    double pitch, f1, f2, f3, gPitch, gF1, gF2, gF3, tilt;

    mDataStore->beginRead();
    {
        auto& pitchTrack = mDataStore->getPitchTrack();

        double end = -1.0;
        auto it = pitchTrack.upper_bound(1e18);
        if (it != pitchTrack.lower_bound(-1e18)) {
            --it;
            end = it->first;
        }
        if (end < 0.0) {
            mDataStore->endRead();
            return;
        }

        pitch = medianDefinedIn(pitchTrack, end - 0.35, end);
        gPitch = medianDefinedIn(pitchTrack, end - 2.0, end);
        tilt = medianDefinedIn(mDataStore->getWeightTrack(), end - 1.0, end, -999.0);

        const int nf = mDataStore->getFormantTrackCount();
        auto fmed = [&](int i, double span) {
            return i < nf ? medianDefinedIn(mDataStore->getFormantTrack(i), end - span, end) : -1.0;
        };
        f1 = fmed(0, 1.0); f2 = fmed(1, 1.0); f3 = fmed(2, 1.0);
        gF1 = fmed(0, 2.0); gF2 = fmed(1, 2.0); gF3 = fmed(2, 2.0);
    }
    mDataStore->endRead();

    QJsonObject msg;
    msg["voiced"] = pitch > 0.0;
    msg["pitch"] = pitch;
    msg["f1"] = f1;
    msg["f2"] = f2;
    msg["f3"] = f3;

    if (gPitch > 0.0 && gF1 > 0.0 && gF2 > 0.0 && gF3 > 0.0) {
        const double pF0 = GenderScore::pitchP(gPitch);
        const double pRes = GenderScore::resonanceP(gF1, gF2, gF3);
        msg["pitchScore"] = pF0;
        msg["resScore"] = pRes;
        msg["score"] = GenderScore::overallP(pF0, pRes);
        // Linear r-space size for the TVL fullness page (~0 masc .. ~1 fem).
        msg["sizeR"] = GenderScore::resonanceR(gF1, gF2, gF3);
        // Calibrated to acousticgender.space's own resonance scale; the
        // genderspace overlay plots this, NOT the (much steeper) resScore.
        const bool deep = mConfig->getFormantAlgorithm() == FormantAlgorithm::Deep;
        msg["resonance"] = GenderScore::siteResonance(gF1, gF2, deep);
    } else {
        msg["pitchScore"] = -1.0;
        msg["resScore"] = -1.0;
        msg["score"] = -1.0;
        msg["resonance"] = -1.0;
    }

    // Vocal weight (TVL): raw tilt in dB/oct plus the 0..1 percept scale.
    if (tilt > -900.0 && tilt < 900.0 && pitch > 0.0) {
        msg["tilt"] = tilt;
        msg["weight"] = GenderScore::weightP(tilt);
    } else {
        msg["tilt"] = -999.0;
        msg["weight"] = -1.0;
    }

    const QString payload = QString::fromUtf8(
            QJsonDocument(msg).toJson(QJsonDocument::Compact));

    for (QWebSocket *sock : mClients) {
        sock->sendTextMessage(payload);
    }
}
