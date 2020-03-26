#include "bookmarks.h"
#include "browser.h"

#include <QBuffer>
#include <QDebug>
#include <QDataStream>
#include <QFile>
#include <QMimeData>
#include <QVBoxLayout>

BookmarkTreeNode::BookmarkTreeNode(BookmarkTreeNode::Type type, BookmarkTreeNode *parent)
    : parent(parent)
    , type(type)
{
    if (parent)
        parent->insert(this);
}

BookmarkTreeNode::~BookmarkTreeNode()
{
    if (parent)
        parent->remove(this);
    qDeleteAll(children);
}

void BookmarkTreeNode::insert(BookmarkTreeNode *child, int index)
{
    if (child->parent)
        child->parent->remove(child);
    child->parent = this;

    if (index == -1)
        index = children.count();
    children.insert(index, child);
}

void BookmarkTreeNode::remove(BookmarkTreeNode *child)
{
    child->parent = nullptr;
    children.removeAll(child);
}

BookmarkModel::BookmarkModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_root_node = new BookmarkTreeNode(BookmarkTreeNode::Root);
}

BookmarkModel::~BookmarkModel()
{
    delete m_root_node;
}

QVariant BookmarkModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0: return QStringLiteral("Title");
        case 1: return QStringLiteral("Address");
        case 2: return QStringLiteral("Description");
        }
    }

    return QAbstractItemModel::headerData(section, orientation, role);
}

QVariant BookmarkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    BookmarkTreeNode *bookmark_node = tree_node(index);

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case 0: return bookmark_node->title;
        case 1: return bookmark_node->address;
        case 2: return bookmark_node->desc;
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == 0) {
            if (bookmark_node->type == BookmarkTreeNode::Folder)
                return QIcon::fromTheme(QStringLiteral("folder"));
            return QIcon::fromTheme(QStringLiteral("text-html"));
        }
        break;
    }

    return QVariant();
}

int BookmarkModel::columnCount(const QModelIndex &parent) const
{
    return parent.column() > 0 ? 0 : 3;
}

int BookmarkModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    if (!parent.isValid())
        return m_root_node->children.count();

    BookmarkTreeNode *node = static_cast<BookmarkTreeNode *>(parent.internalPointer());
    return node->children.count();
}

QModelIndex BookmarkModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    BookmarkTreeNode *parent_node;
    if (!parent.isValid())
        parent_node = m_root_node;
    else
        parent_node = tree_node(parent);

    if (row >= parent_node->children.count())
        return QModelIndex();
    return createIndex(row, column, parent_node->children.at(row));
}

QModelIndex BookmarkModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();

    BookmarkTreeNode *child_node = tree_node(child);
    BookmarkTreeNode *parent_node = child_node->parent;

    if (!parent_node || parent_node == m_root_node)
        return QModelIndex();

    int row = parent_node->parent->children.indexOf(parent_node);
    return createIndex(row, 0, parent_node);
}

Qt::ItemFlags BookmarkModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return QAbstractItemModel::flags(index);

    return Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable | QAbstractItemModel::flags(index);
}

Qt::DropActions BookmarkModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList BookmarkModel::mimeTypes() const
{
    QStringList types;
    types << BOOKMARK_MIMETYPE;
    return types;
}

QMimeData *BookmarkModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mime_data = new QMimeData;
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);

    for (const QModelIndex &index : indexes) {
        if (index.column() != 0 || !index.isValid() || indexes.contains(index.parent()))
            continue;

        QByteArray encoded_data;
        QBuffer buffer(&encoded_data);
        buffer.open(QBuffer::ReadWrite);
        stream << index.row() << (qintptr) index.internalPointer();
    }

    mime_data->setData(BOOKMARK_MIMETYPE, data);
    return mime_data;
}

bool BookmarkModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
    Q_UNUSED(row)

    if (action == Qt::IgnoreAction)
        return true;

    if (!data->hasFormat(BOOKMARK_MIMETYPE) || column > 0)
        return false;

    BookmarkTreeNode *parent_node = tree_node(parent);
    if (parent_node->type != BookmarkTreeNode::Folder)
        return false;

    QByteArray ba = data->data(BOOKMARK_MIMETYPE);
    QDataStream stream(&ba, QIODevice::ReadOnly);
    if (stream.atEnd())
        return false;

    while (!stream.atEnd()) {
        int row;
        qintptr ptr;
        stream >> row >> ptr;

        QModelIndex index = createIndex(row, 0, (void *) ptr);
        BookmarkTreeNode *node = tree_node(index);
        add_bookmark(tree_node(parent), node, row);
    }

    return true;
}

bool BookmarkModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    const QString val = value.toString();
    if (val.isEmpty())
        return false;

    BookmarkTreeNode *node = tree_node(index);

    switch (role) {
    case Qt::EditRole:
    case Qt::DisplayRole:
        switch (index.column()) {
        case 0:
            node->title = val;
            return true;
        case 1:
            node->address = val;
            return true;
        case 2:
            node->desc = val;
            return true;
        }
    }

    return false;
}

void BookmarkModel::add_bookmark(BookmarkTreeNode *parent, BookmarkTreeNode *node, int row)
{
    if (!parent)
        parent = m_root_node;

    if (row == -1)
        row = parent->children.count();

    if (node->parent) {
        BookmarkTreeNode *parent = node->parent;
        int row = parent->children.indexOf(node);
        beginRemoveRows(QModelIndex(), row, row);
        parent->remove(node);
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), row, row);
    parent->insert(node);
    endInsertRows();
}

BookmarkTreeNode *BookmarkModel::tree_node(const QModelIndex &index) const
{
    return static_cast<BookmarkTreeNode *>(index.internalPointer());
}

BookmarkWidget::BookmarkWidget(QWidget *parent)
    : QWidget(parent)
{
    m_tree_view = new QTreeView;
    m_tree_view->setModel(browser->bookmark_model());

    m_tree_view->setDragEnabled(true);
    m_tree_view->setAcceptDrops(true);
    m_tree_view->setDropIndicatorShown(true);

    QVBoxLayout *vbox = new QVBoxLayout;
    setLayout(vbox);
    vbox->addWidget(m_tree_view);
}

XbelReader::XbelReader()
{
}

BookmarkTreeNode *XbelReader::read(const QString &file_name)
{
    QFile file(file_name);
    if (!file.exists())
        return new BookmarkTreeNode(BookmarkTreeNode::Root);

    file.open(QFile::ReadOnly);
    return read(&file);
}

BookmarkTreeNode *XbelReader::read(QIODevice *device)
{
    BookmarkTreeNode *node = new BookmarkTreeNode(BookmarkTreeNode::Root);
    setDevice(device);
    if (readNextStartElement()) {
        QString version = attributes().value(QLatin1String("version")).toString();
        if (name() == QLatin1String("xbel") && (version.isEmpty() || version == QLatin1String("1.0"))) {
            readXBEL(node);
        } else {
            raiseError(QObject::tr("The file is not an XBEL version 1.0 file."));
        }
    }

    return node;
}

void XbelReader::readXBEL(BookmarkTreeNode *node)
{
    while (readNextStartElement()) {
        if (name() == QLatin1String("folder"))
            readFolder(node);
        else if (name() == QLatin1String("bookmark"))
            readBookmarkTreeNode(node);
        else
            skipCurrentElement();
    }
}

void XbelReader::readTitle(BookmarkTreeNode *node)
{
    node->title = readElementText();
}

void XbelReader::readFolder(BookmarkTreeNode *node)
{
    BookmarkTreeNode *folder = new BookmarkTreeNode(BookmarkTreeNode::Folder, node);
    while (readNextStartElement()) {
        if (name() == QLatin1String("title"))
            readTitle(folder);
        else if (name() == QLatin1String("folder"))
            readFolder(folder);
        else if (name() == QLatin1String("bookmark"))
            readBookmarkTreeNode(folder);
        else if (name() == QLatin1String("desc"))
            continue; // TODO
        else
            skipCurrentElement();
    }
}

void XbelReader::readDescription(BookmarkTreeNode *node)
{
    node->desc = readElementText();
}

void XbelReader::readBookmarkTreeNode(BookmarkTreeNode *node)
{
    BookmarkTreeNode *bookmark = new BookmarkTreeNode(BookmarkTreeNode::Address, node);
    bookmark->address = attributes().value(QLatin1String("href")).toString();
    while (readNextStartElement()) {
        if (name() == QLatin1String("title"))
            readTitle(bookmark);
        else if (name() == QLatin1String("desc"))
            readDescription(bookmark);
        else
            skipCurrentElement();
    }

    if (bookmark->title.isEmpty())
        bookmark->title = QObject::tr("Unknown title");
}

XbelWriter::XbelWriter()
{
}

bool XbelWriter::write(const QString &file_name, BookmarkTreeNode *node)
{
    QFile file(file_name);
    if (!node || !file.open(QFile::WriteOnly))
        return false;
    return write(&file, node);
}

bool XbelWriter::write(QIODevice *device, BookmarkTreeNode *node)
{
    setDevice(device);

    writeStartDocument();
    writeDTD(QLatin1String("<!DOCTYPE xbel>"));
    writeStartElement(QLatin1String("xbel"));
    writeAttribute(QLatin1String("version"), QLatin1String("1.0"));
    if (node->type == BookmarkTreeNode::Root) {
        for (int i = 0; i < node->children.count(); i++) {
            write_item(node->children.at(i));
        }
    } else {
        write_item(node);
    }

    writeEndDocument();
    return true;
}

void XbelWriter::write_item(BookmarkTreeNode *node)
{
    switch (node->type) {
    case BookmarkTreeNode::Folder:
        writeStartElement(QLatin1String("folder"));
        writeTextElement(QLatin1String("title"), node->title);
        for (int i = 0; i < node->children.count(); i++) {
            write_item(node->children.at(i));
        }
        writeEndElement();
        break;
    case BookmarkTreeNode::Address:
        writeStartElement(QLatin1String("bookmark"));
        if (!node->address.isEmpty())
            writeAttribute(QLatin1String("href"), node->address);
        writeTextElement(QLatin1String("title"), node->title);
        if (!node->desc.isEmpty())
            writeAttribute(QLatin1String("desc"), node->desc);
        writeEndElement();
    default:
        break;
    }
}