// SPDX-License-Identifier: MIT
// KDE: KPeople (Akonadi, KDE Connect phone contacts, vCards)
#include "contactsource.h"

#include <KPeople/PersonsModel>
#include <KPeopleBackend/AbstractContact>

#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QUrl>

namespace
{
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
