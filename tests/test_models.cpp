// SPDX-License-Identifier: MIT
// The model catalog must be internally consistent: every model pinned by a
// well-formed hash, downloaded over HTTPS from a known host, with a size.
#include "app/ModelCatalog.h"

#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QtTest>

using namespace afterline;

class ModelsTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void catalogIsWellFormed()
    {
        QSet<QString> ids, files, hashes;
        const QRegularExpression sha("^[0-9a-f]{64}$");
        for (const auto &m : modelCatalog()) {
            QVERIFY2(!ids.contains(m.id), qPrintable(m.id));
            QVERIFY(!files.contains(m.fileName));
            QVERIFY(!hashes.contains(m.sha256));
            ids.insert(m.id);
            files.insert(m.fileName);
            hashes.insert(m.sha256);
            QVERIFY2(sha.match(m.sha256).hasMatch(), qPrintable(m.id));
            const QUrl url(m.url);
            QCOMPARE(url.scheme(), QString("https"));
            QCOMPARE(url.host(), QString("huggingface.co"));
            QVERIFY(url.path().endsWith(m.fileName));
            QVERIFY(m.bytes > 100000);
            QVERIFY(!m.license.isEmpty());
            QVERIFY(!m.fileName.contains('/'));
        }
    }

    void everyKindHasARecommendedModel()
    {
        for (ModelKind k : {ModelKind::Speech, ModelKind::SpeechDetector, ModelKind::Notes}) {
            const ModelSpec *m = findModel(recommendedModel(k));
            QVERIFY(m);
            QVERIFY(m->kind == k);
        }
        QVERIFY(findModelBySha256(findModel("whisper-small")->sha256.toUpper()));
        QVERIFY(!findModelBySha256(QString(64, '0')));
    }
};

QTEST_GUILESS_MAIN(ModelsTest)
#include "test_models.moc"
