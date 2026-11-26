// SPDX-License-Identifier: MIT
// KDE: KPeople (Akonadi, KDE Connect phone contacts, vCards)
#include "contactsource.h"

#include <KPeople/PersonsModel>
#include <KPeopleBackend/AbstractContact>

#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace
{
QString decodeQuotedPrintable(const QString &in)
{
    QByteArray out;
    const QByteArray b = in.toLatin1();
    for (int i = 0; i < b.size(); ++i) {
        if (b[i] == '=' && i + 2 < b.size()) {
            bool ok = false;
            const int v = b.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out += char(v);
                i += 2;
                continue;
            }
        }
        out += b[i];
    }
    return QString::fromUtf8(out);
}

QString unescape(QString s)
{
    s.replace(QLatin1String("\\,"), QLatin1String(",")).replace(QLatin1String("\\;"), QLatin1String(";"));
    return s.trimmed();
}

// N (family/given) and the typed TEL lines of a vCard 2.1/3.0/4.0
void parseVCard(const QByteArray &vcard, LocalPerson *p)
{
    QString text = QString::fromUtf8(vcard);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1String("\n "), QString()).replace(QLatin1String("\n\t"), QString()); // folding
    // quoted-printable soft line breaks ("=" at the end), only on QP lines (base64 photos end in "=" too)
    QStringList lines;
    const QStringList raw = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 0; i < raw.size(); ++i) {
        QString line = raw[i];
        if (line.left(line.indexOf(QLatin1Char(':'))).contains(QLatin1String("QUOTED-PRINTABLE"), Qt::CaseInsensitive)) {
            while (line.endsWith(QLatin1Char('=')) && i + 1 < raw.size())
                line = line.chopped(1) + raw[++i];
        }
        lines << line;
    }
    QStringList numbers, kinds;
    for (const QString &line : std::as_const(lines)) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon);
        const QString name = key.section(QLatin1Char(';'), 0, 0).section(QLatin1Char('.'), -1).toUpper();
        const QString params = key.section(QLatin1Char(';'), 1).toUpper();
        QString value = line.mid(colon + 1);
        if (params.contains(QLatin1String("QUOTED-PRINTABLE")))
            value = decodeQuotedPrintable(value);
        if (name == QLatin1String("N")) {
            const QStringList parts = value.split(QRegularExpression(QStringLiteral("(?<!\\\\);")));
            p->family = unescape(parts.value(0));
            p->given = unescape(parts.value(1));
        } else if (name == QLatin1String("TEL") && !value.trimmed().isEmpty()) {
            numbers << value.trimmed();
            kinds << (params.contains(QLatin1String("CELL"))   ? QStringLiteral("mobile")
                          : params.contains(QLatin1String("HOME")) ? QStringLiteral("home")
                          : params.contains(QLatin1String("WORK")) ? QStringLiteral("work")
                                                                   : QString());
        }
    }
    if (!numbers.isEmpty()) {
        p->numbers = numbers;
        p->kinds = kinds;
    }
}

class KPeopleSource : public ContactSource
{
public:
    explicit KPeopleSource(QObject *parent)
        : ContactSource(parent)
        , m_model(new KPeople::PersonsModel(this))
    {
        // reload once initialized and whenever the address book changes (debounced)
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(500);
        connect(timer, &QTimer::timeout, this, &KPeopleSource::reload);
        auto kick = [timer] { timer->start(); };
        connect(m_model, &KPeople::PersonsModel::modelInitialized, this, kick);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, kick);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, kick);
        connect(m_model, &QAbstractItemModel::dataChanged, this, kick);
        connect(m_model, &QAbstractItemModel::modelReset, this, kick);
    }

private:
    void reload()
    {
        QList<LocalPerson> people;
        for (int i = 0; i < m_model->rowCount(); ++i) {
            const QModelIndex ix = m_model->index(i, 0);
            LocalPerson p;
            p.numbers = m_model->contactCustomProperty(ix, KPeople::AbstractContact::AllPhoneNumbersProperty).toStringList();
            if (p.numbers.isEmpty())
                continue;
            p.name = ix.data(KPeople::PersonsModel::FormattedNameRole).toString();
            parseVCard(m_model->contactCustomProperty(ix, KPeople::AbstractContact::VCardProperty).toByteArray(), &p);
            // only real photos (PhotoRole falls back to a generic avatar)
            const QVariant pic = m_model->contactCustomProperty(ix, KPeople::AbstractContact::PictureProperty);
            QImage img;
            if (pic.canConvert<QImage>())
                img = pic.value<QImage>();
            else if (pic.canConvert<QPixmap>())
                img = pic.value<QPixmap>().toImage();
            else if (pic.toUrl().isLocalFile())
                img.load(pic.toUrl().toLocalFile());
            p.photo = photoPng(img);
            people.append(p);
        }
        setPeople(people);
    }

    KPeople::PersonsModel *m_model;
};
}

ContactSource *createKPeopleSource(QObject *parent)
{
    return new KPeopleSource(parent);
}
