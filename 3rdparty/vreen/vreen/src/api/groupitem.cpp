#include "groupitem.h"
#include <QSharedData>
#include <QUrl>

namespace Vreen {

class GroupItemData : public QSharedData {
public:
    GroupItemData() :
        gid(0), is_closed(false),
        is_admin(false), is_member(false),
        type(GroupItem::Type_Unknown)
    {}

    GroupItemData(const GroupItemData &o) : QSharedData(),
        gid(o.gid), name(o.name),
        screenName(o.screenName),
        is_closed(o.is_closed),
        is_admin(o.is_admin),
        is_member(o.is_member),
        type(o.type),
        photos(o.photos)
    {}

    int gid;
    QString name;
    QString screenName;
    bool is_closed;
    bool is_admin;
    bool is_member;
    GroupItem::Type type;
    QMap<Contact::PhotoSize ,QUrl> photos;
};

GroupItem::GroupItem() : data(new GroupItemData)
{
}

GroupItem::GroupItem(const GroupItem &rhs) : data(rhs.data)
{
}

GroupItem &GroupItem::operator=(const GroupItem &rhs)
{
    if (this != &rhs)
        data.operator=(rhs.data);
    return *this;
}

GroupItem::~GroupItem()
{
}

int GroupItem::gid() const
{
    return data->gid;
}

void GroupItem::setGid(int gid)
{
    data->gid = gid;
}

QString GroupItem::name() const
{
    return data->name;
}

void GroupItem::setName(const QString &name)
{
    data->name = name;
}

QString GroupItem::screenName() const
{
    return data ->screenName;
}

void GroupItem::setScreenName(const QString &scr_name)
{
    data->screenName = scr_name;
}

bool GroupItem::isClosed() const
{
    return data->is_closed;
}

void GroupItem::setClosed(bool closed)
{
    data->is_closed = closed;
}

bool GroupItem::isAdmin() const
{
    return data->is_admin;
}

void GroupItem::setAdmin(bool is_admin)
{
    data->is_admin = is_admin;
}

bool GroupItem::isMember() const
{
    return data->is_member;
}

void GroupItem::setMember(bool is_member)
{
    data->is_member = is_member;
}

GroupItem::Type GroupItem::type() const
{
    return data->type;
}

void GroupItem::setType(GroupItem::Type t)
{
    data->type = t;
}

QUrl GroupItem::photo(Contact::PhotoSize size) const
{
    return data->photos[size];
}

void GroupItem::setPhoto(const QUrl &photo, Contact::PhotoSize size)
{
    data->photos[size] = photo;
}

} // namespace Vreen
