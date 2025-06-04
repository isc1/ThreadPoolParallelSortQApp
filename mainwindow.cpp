#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QThreadPool>
#include <QRunnable>
#include <QThread>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QMetaObject>
#include <QApplication>
#include <QRandomGenerator>
#include <QVector> // Added for QVector
// QAtomicInt is typically included via QtCore/qatomic.h or QtCore/qglobal.h
#include <algorithm>
#include <random> // For std::mt19937
#include <atomic> // For std::atomic (though one use case is replaced)
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <functional>

// Global pointer to main window for output
MainWindow* g_mainWindow = nullptr;

// Helper function to append text to the main window (thread-safe)
void appendToOutput(const QString& text) {
    if (g_mainWindow) {
        g_mainWindow->appendOutput(text);
    }
}

// === Task 1: Original Parallel Sort (Refactored to use shared pool) ===

class RandomGenTask : public QRunnable {
private:
    std::vector<int>* data;
    int startIndex;
    int endIndex;
    int maxValue;

public:
    RandomGenTask(std::vector<int>* vec, int start, int end, int maxVal = MainWindow::VECTOR_SIZE)
        : data(vec), startIndex(start), endIndex(end), maxValue(maxVal) {
        setAutoDelete(true);
    }

    void run() override {
        std::random_device rd;
        std::mt19937 gen(rd() ^ ( (std::mt19937::result_type)
                                 std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch()
                                 ).count() +
                                 (std::mt19937::result_type)
                                 std::hash<std::thread::id>()(std::this_thread::get_id()) ) );
        std::uniform_int_distribution<> dis(1, maxValue);

        for (int i = startIndex; i < endIndex; i++) {
            (*data)[i] = dis(gen);
        }
    }
};

class SortTask : public QRunnable {
private:
    std::vector<int>* data;
    int startIndex;
    int endIndex;
    int taskId;

public:
    SortTask(std::vector<int>* vec, int start, int end, int id)
        : data(vec), startIndex(start), endIndex(end), taskId(id) {
        setAutoDelete(true);
    }

    void run() override {
        QString startMsg = QString("[Thread %1] Task %2 sorting range [%3-%4)")
                          .arg((quintptr)QThread::currentThreadId())
                          .arg(taskId)
                          .arg(startIndex)
                          .arg(endIndex);
        appendToOutput(startMsg);

        std::sort(data->begin() + startIndex, data->begin() + endIndex);
        if (MainWindow::USE_PCT_CORE < 100) {
            int delayMs = (100 - MainWindow::USE_PCT_CORE) * 2;
            QThread::msleep(delayMs);
        }

        QString endMsg = QString("[Thread %1] Task %2 completed sorting")
                        .arg((quintptr)QThread::currentThreadId())
                        .arg(taskId);
        appendToOutput(endMsg);
    }
};

class MergeTask : public QRunnable {
private:
    std::vector<int>* data;
    int start1, end1;
    int start2, end2;
    int taskId;

public:
    MergeTask(std::vector<int>* vec, int s1, int e1, int s2, int e2, int id)
        : data(vec), start1(s1), end1(e1), start2(s2), end2(e2), taskId(id) {
        setAutoDelete(true);
    }

    void run() override {
        QString startMsg = QString("[Thread %1] Merge Task %2 merging ranges [%3-%4) and [%5-%6)")
                          .arg((quintptr)QThread::currentThreadId())
                          .arg(taskId)
                          .arg(start1)
                          .arg(end1)
                          .arg(start2)
                          .arg(end2);
        appendToOutput(startMsg);

        std::vector<int> temp;
        temp.reserve((end1 - start1) + (end2 - start2));
        std::merge(data->begin() + start1, data->begin() + end1,
                   data->begin() + start2, data->begin() + end2,
                   std::back_inserter(temp));
        std::copy(temp.begin(), temp.end(), data->begin() + start1);
        if (MainWindow::USE_PCT_CORE < 100) {
            int delayMs = (100 - MainWindow::USE_PCT_CORE) * 2;
            QThread::msleep(delayMs);
        }

        QString endMsg = QString("[Thread %1] Merge Task %2 completed")
                        .arg((quintptr)QThread::currentThreadId())
                        .arg(taskId);
        appendToOutput(endMsg);
    }
};

class ParallelSorter {
private:
    QThreadPool* m_pool;    // Declared first
    std::vector<int>* data; // Declared second
public:
    // Initializer list order matches declaration order
    ParallelSorter(std::vector<int>* vec, QThreadPool* pool) : m_pool(pool), data(vec) {
        appendToOutput(QString("ParallelSorter using shared pool with max %1 threads.").arg(m_pool->maxThreadCount()));
        appendToOutput(QString("Core utilization set to %1%").arg(MainWindow::USE_PCT_CORE));
        appendToOutput(QString("Main thread ID: %1").arg((quintptr)QThread::currentThreadId()));
    }

    void parallelSort() {
        int vectorSize = data->size();
        int numThreads = m_pool->maxThreadCount();
        if (numThreads == 0) {
            appendToOutput("Error: Thread pool has 0 max threads. Cannot sort.");
            return;
        }
        int chunkSize = (vectorSize > 0 && numThreads > 0) ? std::max(1, vectorSize / numThreads) : 1;

        appendToOutput("=== PHASE 1: Sorting chunks in parallel ===");
        appendToOutput(QString("Vector size: %1").arg(vectorSize));
        appendToOutput(QString("Chunk size: %1 (numThreads: %2)").arg(chunkSize).arg(numThreads));

        for (int i = 0; i < numThreads; i++) {
            int start = i * chunkSize;
            int end = (i == numThreads - 1) ? vectorSize : (i + 1) * chunkSize;
            if (start >= vectorSize) break;
            end = std::min(end, vectorSize);
            if (start >= end) continue;

            SortTask* task = new SortTask(data, start, end, i);
            m_pool->start(task);
        }

        while (!m_pool->waitForDone(100)) {
            QApplication::processEvents();
        }

        appendToOutput("=== PHASE 2: Merging sorted chunks ===");
        std::vector<std::pair<int, int>> chunks;
        for (int i = 0; i < numThreads; i++) {
            int start = i * chunkSize;
            int end = (i == numThreads - 1) ? vectorSize : (i + 1) * chunkSize;
            if (start >= vectorSize) break;
            end = std::min(end, vectorSize);
            if (start >= end) continue;
            chunks.push_back({start, end});
        }

        int mergeTaskId = 0;
        while (chunks.size() > 1) {
            std::vector<std::pair<int, int>> newChunks;
            for (size_t i = 0; i < chunks.size(); i += 2) {
                if (i + 1 < chunks.size()) {
                    int start1 = chunks[i].first;
                    int end1 = chunks[i].second;
                    int start2 = chunks[i + 1].first;
                    int end2 = chunks[i + 1].second;
                    MergeTask* mergeTask = new MergeTask(data, start1, end1, start2, end2, mergeTaskId++);
                    m_pool->start(mergeTask);
                    newChunks.push_back({start1, end2});
                } else {
                    newChunks.push_back(chunks[i]);
                }
            }
            while (!m_pool->waitForDone(100)) {
                QApplication::processEvents();
            }
            chunks = newChunks;
        }
        appendToOutput("=== Sorting complete! ===");
    }
};

bool isSorted(const std::vector<int>& vec) {
    for (size_t i = 1; i < vec.size(); i++) {
        if (vec[i] < vec[i - 1]) {
            return false;
        }
    }
    return true;
}

void printSample(const std::vector<int>& vec, const QString& label) {
    appendToOutput(label);
    QString firstElements = "First 10 elements: ";
    int firstCount = std::min(10, (int)vec.size());
    for (int i = 0; i < firstCount; i++) {
        firstElements += QString::number(vec[i]);
        if (i < firstCount - 1) firstElements += ", ";
    }
    appendToOutput(firstElements);

    if (vec.empty()) return;

    QString lastElements = "Last 10 elements: ";
    int lastCount = std::min(10, (int)vec.size());
    for (size_t i = std::max(0, (int)vec.size() - lastCount); i < vec.size(); i++) {
        lastElements += QString::number(vec[i]);
        if (i < vec.size() - 1) lastElements += ", ";
    }
    appendToOutput(lastElements);
}


// === Task 2: String Matrix Population and Sorting ===

QString generateRandomString(int length) {
    const QString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    QString randomString;
    randomString.reserve(length);
    for (int i = 0; i < length; ++i) {
        int index = QRandomGenerator::global()->bounded(possibleCharacters.length());
        randomString.append(possibleCharacters.at(index));
    }
    return randomString;
}

class PopulateStringRowTask : public QRunnable {
private:
    std::vector<std::vector<QString>>* m_matrix;
    int m_rowIndex;
    int m_numCols;
    int m_stringLength;

public:
    PopulateStringRowTask(std::vector<std::vector<QString>>* matrix, int rowIndex, int numCols, int stringLength)
        : m_matrix(matrix), m_rowIndex(rowIndex), m_numCols(numCols), m_stringLength(stringLength) {
        setAutoDelete(true);
    }

    void run() override {
        std::vector<QString>& row = (*m_matrix)[m_rowIndex];
        row.resize(m_numCols);
        for (int j = 0; j < m_numCols; ++j) {
            row[j] = generateRandomString(m_stringLength);
        }
    }
};

class SortStringRowTask : public QRunnable {
private:
    std::vector<std::vector<QString>>* m_matrix;
    int m_rowIndex;

public:
    SortStringRowTask(std::vector<std::vector<QString>>* matrix, int rowIndex)
        : m_matrix(matrix), m_rowIndex(rowIndex) {
        setAutoDelete(true);
    }

    void run() override {
        std::sort((*m_matrix)[m_rowIndex].begin(), (*m_matrix)[m_rowIndex].end());
    }
};

class StringMatrixProcessor {
private:
    std::vector<std::vector<QString>>* m_matrix;
    QThreadPool* m_pool;
    int m_numRows;
    int m_numCols;
    int m_stringLength;

public:
    StringMatrixProcessor(std::vector<std::vector<QString>>* matrix, QThreadPool* pool, int rows, int cols, int strLen)
        : m_matrix(matrix), m_pool(pool), m_numRows(rows), m_numCols(cols), m_stringLength(strLen) {}

    void populate() {
        appendToOutput(QString("Populating %1x%2 string matrix with %3-char strings...").arg(m_numRows).arg(m_numCols).arg(m_stringLength));
        for (int i = 0; i < m_numRows; ++i) {
            PopulateStringRowTask* task = new PopulateStringRowTask(m_matrix, i, m_numCols, m_stringLength);
            m_pool->start(task);
        }
        while (!m_pool->waitForDone(100)) { QApplication::processEvents(); }
        appendToOutput("String matrix population complete.");
    }

    void sortRows() {
        appendToOutput(QString("Sorting %1 rows of string matrix...").arg(m_numRows));
        for (int i = 0; i < m_numRows; ++i) {
            SortStringRowTask* task = new SortStringRowTask(m_matrix, i);
            m_pool->start(task);
        }
        while (!m_pool->waitForDone(100)) { QApplication::processEvents(); }
        appendToOutput("String matrix row sorting complete.");
    }
};


// === Task 3: Decrement Vector Elements ===

class PopulateDecrementVectorTask : public QRunnable {
private:
    std::vector<int>* m_data;
    int m_startIndex;
    int m_endIndex;
    int m_maxValue;
public:
    PopulateDecrementVectorTask(std::vector<int>* data, int start, int end, int maxValue)
        : m_data(data), m_startIndex(start), m_endIndex(end), m_maxValue(maxValue) {
        setAutoDelete(true);
    }
    void run() override {
        std::random_device rd;
        std::mt19937 gen(rd() ^ ( (std::mt19937::result_type)
                                 std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch()
                                 ).count() +
                                 (std::mt19937::result_type)
                                 std::hash<std::thread::id>()(std::this_thread::get_id()) ) );
        std::uniform_int_distribution<> dis(1, m_maxValue);
        for (int i = m_startIndex; i < m_endIndex; ++i) {
            (*m_data)[i] = dis(gen);
        }
    }
};

class DecrementChunkTask : public QRunnable {
private:
    std::vector<int>* m_data;
    int m_startIndex;
    int m_endIndex;
    QAtomicInt* m_chunkNonZeroCount; // Changed to QAtomicInt*

public:
    DecrementChunkTask(std::vector<int>* data, int start, int end, QAtomicInt* chunkNonZeroCount) // Changed type
        : m_data(data), m_startIndex(start), m_endIndex(end), m_chunkNonZeroCount(chunkNonZeroCount) {
        setAutoDelete(true);
    }

    void run() override {
        int currentNonZero = 0; // QAtomicInt operates on int
        QRandomGenerator random = QRandomGenerator::securelySeeded();

        for (int i = m_startIndex; i < m_endIndex; ++i) {
            if ((*m_data)[i] > 0) {
                if (random.bounded(2) == 0) { // 50% chance
                    (*m_data)[i]--;
                }
                if ((*m_data)[i] > 0) {
                    currentNonZero++;
                }
            }
        }
        m_chunkNonZeroCount->store(currentNonZero); // Use QAtomicInt API
    }
};

class DecrementProcessor {
private:
    std::vector<int>* m_data;
    QThreadPool* m_pool;
    int m_vectorSize;
    QVector<QAtomicInt> m_chunkNonZeroCounts; // Changed to QVector<QAtomicInt>

public:
    DecrementProcessor(std::vector<int>* data, QThreadPool* pool, int vectorSize)
        : m_data(data), m_pool(pool), m_vectorSize(vectorSize) {}

    void populateVector(int maxValue) {
        appendToOutput(QString("Populating vector of size %1 with random values up to %2 for decrement task...").arg(m_vectorSize).arg(maxValue));
        int numThreads = m_pool->maxThreadCount();
        if (numThreads == 0) { appendToOutput("Error: Thread pool has 0 threads for population."); return; }
        int chunkSize = (m_vectorSize > 0 && numThreads > 0) ? std::max(1, m_vectorSize / numThreads) : 1;

        for (int i = 0; i < numThreads; ++i) {
            int start = i * chunkSize;
            int end = (i == numThreads - 1) ? m_vectorSize : (i + 1) * chunkSize;
            if (start >= m_vectorSize) break;
            end = std::min(end, m_vectorSize);
            if (start >= end) continue;

            PopulateDecrementVectorTask* task = new PopulateDecrementVectorTask(m_data, start, end, maxValue);
            m_pool->start(task);
        }
        while (!m_pool->waitForDone(100)) { QApplication::processEvents(); }
        appendToOutput("Decrement vector population complete.");
    }

    qint64 decrementToZero() {
        appendToOutput("Starting decrement process...");
        QElapsedTimer timer;
        timer.start();

        int numThreads = m_pool->maxThreadCount();
        if (numThreads == 0) {
            appendToOutput("Error: Thread pool has 0 threads for decrementing.");
            return -1;
        }

        m_chunkNonZeroCounts.clear();
        // This resize call should default-construct 'numThreads' QAtomicInt objects.
        // Default construction for QAtomicInt initializes it to zero.
        m_chunkNonZeroCounts.resize(numThreads);


        int passCount = 0;
        while (true) {
            passCount++;
            long long totalNonZeroElementsInPass = 0; // Sum can be larger than int
            int chunkSize = (m_vectorSize > 0 && numThreads > 0) ? std::max(1, m_vectorSize / numThreads) : 1;

            for (int i = 0; i < numThreads; ++i) {
                int start = i * chunkSize;
                int end = (i == numThreads - 1) ? m_vectorSize : (i + 1) * chunkSize;
                if (start >= m_vectorSize) break;
                end = std::min(end, m_vectorSize);
                if (start >= end) continue;

                if (i < m_chunkNonZeroCounts.size()) { // QVector uses int for size and index
                    DecrementChunkTask* task = new DecrementChunkTask(m_data, start, end, &m_chunkNonZeroCounts[i]);
                    m_pool->start(task);
                } else {
                     appendToOutput(QString("Error: Task index %1 out of bounds for m_chunkNonZeroCounts (size %2). Skipping task.")
                               .arg(i).arg(m_chunkNonZeroCounts.size()));
                }
            }

            while (!m_pool->waitForDone(100)) { QApplication::processEvents(); }

            for (int i = 0; i < numThreads; ++i) {
                 int start = i * chunkSize;
                 if (start >= m_vectorSize) break;

                if (i < m_chunkNonZeroCounts.size()) {
                   totalNonZeroElementsInPass += m_chunkNonZeroCounts[i].load(); // Use QAtomicInt API
                }
            }

            appendToOutput(QString("Decrement Pass %1: %2 elements remaining > 0.").arg(passCount).arg(totalNonZeroElementsInPass));

            if (totalNonZeroElementsInPass == 0) {
                break;
            }
            QApplication::processEvents();
        }

        qint64 elapsed = timer.elapsed();
        appendToOutput(QString("Decrement process complete. All elements are zero. Took %1 passes.").arg(passCount));
        return elapsed;
    }
};


// === MainWindow Implementation ===
MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Parallel Tasks Demo");
    setFixedSize(800, 700);
    g_mainWindow = this;

    m_sharedThreadPool = new QThreadPool(this);
    int totalCores = QThread::idealThreadCount();
    int usableCores = std::max(1, totalCores > 1 ? totalCores - 1 : 1);
    m_sharedThreadPool->setMaxThreadCount(usableCores);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    statusLabel = new QLabel("Select a task to begin.");
    statusLabel->setWordWrap(true);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    startButton = new QPushButton("Start Number Sort (Task 1)");
    startStringMatrixButton = new QPushButton("Start String Matrix (Task 2)");
    startDecrementButton = new QPushButton("Start Decrement Task (Task 3)");
    clearButton = new QPushButton("Clear Output");

    buttonLayout->addWidget(startButton);
    buttonLayout->addWidget(startStringMatrixButton);
    buttonLayout->addWidget(startDecrementButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(clearButton);

    outputText = new QTextEdit();
    outputText->setReadOnly(true);
    outputText->setFont(QFont("Courier", 9));

    mainLayout->addWidget(statusLabel);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(outputText, 1);

    connect(startButton, &QPushButton::clicked, this, &MainWindow::runSortingDemo);
    connect(startStringMatrixButton, &QPushButton::clicked, this, &MainWindow::runStringMatrixTask);
    connect(startDecrementButton, &QPushButton::clicked, this, &MainWindow::runDecrementTask);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearOutput);

    appendToOutput(QString("GUI Application started. Shared thread pool configured with %1 max threads.").arg(usableCores));
    appendToOutput(QString("System has %1 ideal cores.").arg(totalCores));
}

MainWindow::~MainWindow() {
    g_mainWindow = nullptr;
}

void MainWindow::appendOutput(const QString& text) {
    QMetaObject::invokeMethod(this, [this, text]() {
        outputText->append(text);
        outputText->ensureCursorVisible();
    }, Qt::QueuedConnection);
}

void MainWindow::clearOutput() {
    outputText->clear();
    appendOutput("Output cleared. Ready for next demo!");
}

void MainWindow::runSortingDemo() {
    startButton->setEnabled(false);
    startStringMatrixButton->setEnabled(false);
    startDecrementButton->setEnabled(false);
    statusLabel->setText("Task 1 (Number Sort) in progress... Watch output.");
    appendOutput("\n" + QString("=").repeated(60));
    appendOutput("STARTING TASK 1: PARALLEL NUMBER SORTING DEMO");
    appendOutput(QString("=").repeated(60));

    data.resize(VECTOR_SIZE);
    appendToOutput(QString("Generating %1 random integers using shared pool...").arg(VECTOR_SIZE));

    int numGenThreads = m_sharedThreadPool->maxThreadCount();
    if (numGenThreads == 0) {
        appendToOutput("Error: Cannot generate numbers, pool has 0 threads.");
        startButton->setEnabled(true);
        startStringMatrixButton->setEnabled(true);
        startDecrementButton->setEnabled(true);
        statusLabel->setText("Error: Thread pool unavailable. Select a task.");
        return;
    }
    int genChunkSize = (VECTOR_SIZE > 0 && numGenThreads > 0) ? std::max(1, VECTOR_SIZE / numGenThreads) : 1;

    for (int i = 0; i < numGenThreads; i++) {
        int start = i * genChunkSize;
        int end = (i == numGenThreads - 1) ? VECTOR_SIZE : (i + 1) * genChunkSize;
        if (start >= VECTOR_SIZE) break;
        end = std::min(end, (int)VECTOR_SIZE);
        if (start >= end) continue;
        RandomGenTask* genTask = new RandomGenTask(&data, start, end);
        m_sharedThreadPool->start(genTask);
    }
    while (!m_sharedThreadPool->waitForDone(100)) { QApplication::processEvents(); }

    printSample(data, "\nOriginal vector (unsorted):");

    QElapsedTimer timer;
    timer.start();

    ParallelSorter sorter(&data, m_sharedThreadPool);
    sorter.parallelSort();

    qint64 parallelTime = timer.elapsed();
    bool sorted = isSorted(data);
    appendOutput(QString("\nVector is sorted: %1").arg(sorted ? "true" : "false"));
    printSample(data, "\nSorted vector:");
    appendOutput(QString("\nParallel sort took: %1 ms").arg(parallelTime));

    appendOutput("\nNow testing single-threaded sort for comparison...");
    appendOutput("Regenerating random data using shared pool...");

    for (int i = 0; i < numGenThreads; i++) {
        int start = i * genChunkSize;
        int end = (i == numGenThreads - 1) ? VECTOR_SIZE : (i + 1) * genChunkSize;
        if (start >= VECTOR_SIZE) break;
        end = std::min(end, (int)VECTOR_SIZE);
        if (start >= end) continue;
        RandomGenTask* genTask = new RandomGenTask(&data, start, end);
        m_sharedThreadPool->start(genTask);
    }
    while (!m_sharedThreadPool->waitForDone(100)) { QApplication::processEvents(); }

    timer.restart();
    std::sort(data.begin(), data.end());
    qint64 singleThreadTime = timer.elapsed();
    appendOutput(QString("Single-threaded sort took: %1 ms").arg(singleThreadTime));

    if (parallelTime > 0) {
        double speedup = (double)singleThreadTime / parallelTime;
        appendOutput(QString("Speedup: %1x").arg(speedup, 0, 'f', 2));
    } else {
        appendOutput("Speedup: N/A (Parallel time was zero or negative)");
    }

    appendOutput(QString("=").repeated(60));
    appendOutput("TASK 1 (NUMBER SORT) COMPLETE");
    appendOutput(QString("=").repeated(60) + "\n");

    statusLabel->setText("Task 1 complete! Select a task to begin.");
    startButton->setEnabled(true);
    startStringMatrixButton->setEnabled(true);
    startDecrementButton->setEnabled(true);
}

void MainWindow::printStringMatrixSample(const QString& label) {
    appendToOutput(label);
    if (stringData.empty()) {
        appendToOutput("String matrix is empty.");
        return;
    }

    for (int i = 0; i < std::min((int)stringData.size(), 3); ++i) {
        QString rowStr = QString("Row %1 (first 5 elements): ").arg(i);
        if (stringData[i].empty()) {
            rowStr += "[empty]";
        } else {
            for (int j = 0; j < std::min((int)stringData[i].size(), 5); ++j) {
                rowStr += stringData[i][j];
                if (j < std::min((int)stringData[i].size(), 5) - 1) rowStr += ", ";
            }
        }
        appendToOutput(rowStr);
    }
}

void MainWindow::runStringMatrixTask() {
    startButton->setEnabled(false);
    startStringMatrixButton->setEnabled(false);
    startDecrementButton->setEnabled(false);
    statusLabel->setText("Task 2 (String Matrix) in progress... Watch output.");
    appendOutput("\n" + QString("=").repeated(60));
    appendOutput("STARTING TASK 2: STRING MATRIX POPULATION AND SORT");
    appendOutput(QString("=").repeated(60));

    stringData.assign(STRING_MATRIX_ROWS, std::vector<QString>());

    StringMatrixProcessor processor(&stringData, m_sharedThreadPool, STRING_MATRIX_ROWS, STRING_MATRIX_COLS, STRING_LENGTH);

    QElapsedTimer timer;
    timer.start();

    processor.populate();
    qint64 populateTime = timer.elapsed();
    appendToOutput(QString("String matrix population took: %1 ms").arg(populateTime));
    printStringMatrixSample("\nSample of populated string matrix (before sort):");

    timer.restart();
    processor.sortRows();
    qint64 sortTime = timer.elapsed();
    appendToOutput(QString("String matrix row sorting took: %1 ms").arg(sortTime));
    printStringMatrixSample("\nSample of sorted string matrix:");

    qint64 totalTime = populateTime + sortTime;
    appendToOutput(QString("\nTotal time for Task 2: %1 ms").arg(totalTime));

    appendOutput(QString("=").repeated(60));
    appendOutput("TASK 2 (STRING MATRIX) COMPLETE");
    appendOutput(QString("=").repeated(60) + "\n");

    statusLabel->setText("Task 2 complete! Select a task to begin.");
    startButton->setEnabled(true);
    startStringMatrixButton->setEnabled(true);
    startDecrementButton->setEnabled(true);
}

bool MainWindow::verifyAllZero(const std::vector<int>& vec) {
    for (int val : vec) {
        if (val != 0) return false;
    }
    return true;
}

void MainWindow::runDecrementTask() {
    startButton->setEnabled(false);
    startStringMatrixButton->setEnabled(false);
    startDecrementButton->setEnabled(false);
    statusLabel->setText("Task 3 (Decrement Vector) in progress... Watch output.");
    appendOutput("\n" + QString("=").repeated(60));
    appendOutput("STARTING TASK 3: DECREMENT VECTOR ELEMENTS TO ZERO");
    appendOutput(QString("=").repeated(60));

    data.assign(DECREMENT_VECTOR_SIZE, 0);

    DecrementProcessor processor(&data, m_sharedThreadPool, DECREMENT_VECTOR_SIZE);

    processor.populateVector(MAX_RANDOM_VALUE_DECREMENT);
    printSample(data, "\nInitial vector for decrement task (first/last 10 elements):");

    qint64 decrementTime = processor.decrementToZero();

    if (decrementTime >= 0) {
        appendToOutput(QString("\nTotal time for decrement phase: %1 ms").arg(decrementTime));
        bool allZero = verifyAllZero(data);
        appendToOutput(QString("Verification: All elements are zero = %1").arg(allZero ? "true" : "false"));
        if (!allZero) {
             printSample(data, "\nSample of vector after decrement (if not all zero):");
        }
    } else {
        appendToOutput("\nDecrement task failed or was interrupted.");
    }

    appendOutput(QString("=").repeated(60));
    appendOutput("TASK 3 (DECREMENT VECTOR) COMPLETE");
    appendOutput(QString("=").repeated(60) + "\n");

    statusLabel->setText("Task 3 complete! Select a task to begin.");
    startButton->setEnabled(true);
    startStringMatrixButton->setEnabled(true);
    startDecrementButton->setEnabled(true);
}
