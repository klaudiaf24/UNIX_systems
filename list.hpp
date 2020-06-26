#ifndef LIST_HPP
#define LIST_HPP

#include <string>
#include <utility>

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>
#include <assert.h>

class List {
private:
    struct Node {
        std::ptrdiff_t next;
        int data;
        std::ptrdiff_t previous;
    };

    struct NodeBlock {
        char nd[sizeof(Node)];
        int taken;
    };

    struct ListData {
        std::ptrdiff_t head;
        std::ptrdiff_t tail;
        std::size_t size = 0;
        std::mutex listMutex;

        std::size_t blocks_count;
    };

    // memory from shm_open and mmap
    // ListData | capacity * NodeBlock
    void *m_data_ptr;
    std::string m_shmget_name;

    ListData *getListData();
    NodeBlock *getBlocks();
    Node *getNode(std::ptrdiff_t offset);
    static std::size_t getMmapSize(std::size_t size);
    std::ptrdiff_t get_free_node_offset();
    void return_node(std::ptrdiff_t offset);
    List(void *_data_ptr, const std::string &name);

public:
    // size must be always the same, otherwise behaviour is undefined
    static List newList(const std::string &name, std::size_t size);
    static List useList(const std::string &name, std::size_t size);
    List(const List &) = delete;
    List(List &&other);
    List &operator=(const List &) = delete;
    List &operator=(List &&) = delete;
    ~List();
    void delete_list_data();
    void push_back(int n);
    void pop_back();
    void pop_front();

    class Iterator {
        friend class List;

    private:
        std::ptrdiff_t m_node_offset;
        List *m_list;

        Iterator(List *list, std::ptrdiff_t offset);

    public:
        Iterator();
        bool operator==(const Iterator &) const;
        bool operator!=(const Iterator &) const;
        int &operator*();
        Iterator &operator++();
        Iterator operator++(int);
    };

    Iterator begin();
    Iterator end();
    ListData *getData();
    void swap_in_memory(Iterator first, Iterator second);
};

std::size_t List::getMmapSize(std::size_t size) {
    return sizeof(ListData) + sizeof(NodeBlock) * size;
}

List::ListData *List::getListData() {
    return static_cast<ListData *>(m_data_ptr);
}

List::NodeBlock *List::getBlocks() {
    return reinterpret_cast<NodeBlock *>(static_cast<char *>(m_data_ptr) + sizeof(ListData));
}

List::Node *List::getNode(std::ptrdiff_t offset) {
    auto result = static_cast<char *>(m_data_ptr) + offset;
    return reinterpret_cast<Node *>(result);
}

List::List(void *_data, const std::string &_shmget_name) {
    m_data_ptr = _data;
    m_shmget_name = _shmget_name;
}

List List::newList(const std::string &name, std::size_t size) {
    auto shmget_name = '/' + name;
    auto fd = shm_open(shmget_name.c_str(), O_RDWR | O_CREAT, 0777);
    if (fd == -1) {
        perror("newList shm_open");
        exit(10);
    }
    auto ftrun = ftruncate(fd, getMmapSize(size));
    if (ftrun == -1) {
        perror("useList ftruncate");
        exit(10);
    }
    auto ptr = mmap(NULL, getMmapSize(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("newList mmap");
        exit(10);
    }
    auto list_ptr = new(reinterpret_cast<ListData *>(ptr)) ListData{};
    list_ptr->blocks_count = size;

    return List(ptr, shmget_name);
}

List List::useList(const std::string &name, std::size_t size) {
    auto shmget_name = '/' + name;
    auto fd = shm_open(shmget_name.c_str(), O_RDWR, 0777);
    if (fd == -1) {
        perror("useList shm_open");
        exit(10);
    }
    auto ptr = mmap(NULL, getMmapSize(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        perror("useList mmap");
        exit(10);
    }
    return List(ptr, shmget_name);
}

List::List(List &&other) : List(nullptr, std::string()) {
    std::swap(m_data_ptr, other.m_data_ptr);
    std::swap(m_shmget_name, other.m_shmget_name);
}

List::~List() {
    if (m_data_ptr) {
        munmap(m_data_ptr, getMmapSize(getListData()->blocks_count));
        shm_unlink(m_shmget_name.c_str());
    }
}

void List::delete_list_data() {
    // delete all elements
    auto iter = getListData()->head;
    while (iter != 0) {
        auto tmp = getNode(iter)->next;
        getNode(iter)->~Node();
        iter = tmp;
    }
    getListData()->~ListData();
}

void List::push_back(int n) {
    assert(getListData()->size != getListData()->blocks_count);

    std::unique_lock(getListData()->listMutex);
    auto new_node_offset = get_free_node_offset();
    auto new_node = getNode(new_node_offset);

    new(new_node) Node{};
    new_node->data = n;
    new_node->next = 0;
    new_node->previous = 0;

    if (!getListData()->head) {
        getListData()->head = new_node_offset;
    } else {
        new_node->previous = getListData()->tail;
        getNode(getListData()->tail)->next = new_node_offset;
    }
    getListData()->tail = new_node_offset;
    getListData()->size++;
}

void List::pop_back() {
    std::unique_lock(getListData()->listMutex);

    auto new_tail = getNode(getListData()->tail)->previous;
    getNode(getListData()->tail)->~Node();
    return_node(getListData()->tail);
    if (!new_tail)
        getListData()->head = 0;
    else
        getNode(new_tail)->next = 0;
    getListData()->tail = new_tail;
    getListData()->size--;
}

void List::pop_front() {
    std::unique_lock(getListData()->listMutex);

    auto new_head = getNode(getListData()->head)->next;
    getNode(getListData()->head)->~Node();
    return_node(getListData()->head);
    if (!new_head)
        getListData()->tail = 0;
    else
        getNode(new_head)->previous = 0;
    getListData()->head = new_head;
    getListData()->size--;
}

std::ptrdiff_t List::get_free_node_offset() {
    auto data = getListData();
    auto blocks = getBlocks();
    for (int iter = 0; iter < data->blocks_count; ++iter) {
        if (!blocks[iter].taken) {
            blocks[iter].taken = 1;
            return (blocks[iter].nd - static_cast<char *>(m_data_ptr));
        }
    }
    assert(0);
    return 0;
}

void List::return_node(std::ptrdiff_t offset) {
    NodeBlock *node_block = reinterpret_cast<NodeBlock *>(getNode(offset));
    node_block->taken = 0;
}

List::Iterator::Iterator(List *list, std::ptrdiff_t offset) : m_node_offset(offset), m_list(list) { }

List::Iterator::Iterator() : Iterator(nullptr, 0) {}

bool List::Iterator::operator==(const Iterator &other) const {
    return m_node_offset == other.m_node_offset && (!m_node_offset || m_list == other.m_list);
}

bool List::Iterator::operator!=(const Iterator &other) const {
    return !(*this == other);
}

int &List::Iterator::operator*() {
    return m_list->getNode(m_node_offset)->data;
}

List::Iterator &List::Iterator::operator++() {
    m_node_offset = m_list->getNode(m_node_offset)->next;
    return *this;
}

List::Iterator List::Iterator::operator++(int) {
    auto tmp = *this;
    ++(*this);
    return tmp;
}

List::Iterator List::begin() {
    return Iterator(this, getListData()->head);
}

List::Iterator List::end() {
    return Iterator();
}

List::ListData *List::getData() {
    return getListData();
}

void List::swap_in_memory(Iterator first, Iterator second) {
    assert(first != second);

    std::unique_lock(getListData()->listMutex);
    auto first_offset = first.m_node_offset;
    auto second_offset = second.m_node_offset;
    auto first_node = getNode(first_offset);
    auto second_node = getNode(second_offset);

    auto first_next_offset = first_node->next;
    auto second_next_offset = second_node->next;
    auto first_previous_offset = first_node->previous;
    auto second_previous_offset = second_node->previous;


    if (first_next_offset)
        getNode(first_next_offset)->previous = second_offset;
    else
        getListData()->tail = second_offset;

    if (second_next_offset)
        getNode(second_next_offset)->previous = first_offset;
    else
        getListData()->tail = first_offset;

    if (first_previous_offset)
        getNode(first_previous_offset)->next = second_offset;
    else
        getListData()->head = second_offset;

    if (second_previous_offset)
        getNode(second_previous_offset)->next = first_offset;
    else
        getListData()->head = first_offset;

    // swap all elements
    std::swap(first_node->next, second_node->next);
    std::swap(first_node->data, second_node->data);
    std::swap(first_node->previous, second_node->previous);
}

#endif // LIST_HPP
