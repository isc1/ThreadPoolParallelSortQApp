// === mainwindow.cpp ===
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
#include <algorithm>
#include <random>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <sstream>

// Forward declaration
class MainWindow;

// Global pointer to main window for output
MainWindow* g_mainWindow = nullptr;

// Helper function to append text to the main window (thread-safe)
void appendToOutput(const QString& text) {
    if (g_mainWindow) {
        g_mainWindow->appendOutput(text);
    }
}

// Task to sort a chunk of the vector
class SortTask : public QRunnable {
private:
    std::vector<int>* data;
    int startIndex;
    int endIndex;
    int taskId;

public:
    SortTask(std::vector<int>* vec, int start, int end, int id)
        : data(vec), startIndex(start), endIndex(end), taskId(id) {
        setAutoDelete(true);  // QThreadPool will delete this after run()
    }

    void run() override {
        QString startMsg = QString("[Thread %1] Task %2 sorting range [%3-%4)")
                          .arg((quintptr)QThread::currentThreadId())
                          .arg(taskId)
                          .arg(startIndex)
                          .arg(endIndex);
        appendToOutput(startMsg);

        // Sort this chunk of the vector
        std::sort(data->begin() + startIndex, data->begin() + endIndex);

        QString endMsg = QString("[Thread %1] Task %2 completed sorting")
                        .arg((quintptr)QThread::currentThreadId())
                        .arg(taskId);
        appendToOutput(endMsg);
    }
};

// Task to merge sorted chunks
class MergeTask : public QRunnable {
private:
    std::vector<int>* data;
    int start1, end1;  // First sorted range
    int start2, end2;  // Second sorted range
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

        // Create temporary vector for merged result
        std::vector<int> temp;
        temp.reserve(end2 - start1);

        // Merge the two sorted ranges
        std::merge(data->begin() + start1, data->begin() + end1,
                   data->begin() + start2, data->begin() + end2,
                   std::back_inserter(temp));

        // Copy back to original vector
        std::copy(temp.begin(), temp.end(), data->begin() + start1);

        QString endMsg = QString("[Thread %1] Merge Task %2 completed")
                        .arg((quintptr)QThread::currentThreadId())
                        .arg(taskId);
        appendToOutput(endMsg);
    }
};

class ParallelSorter {
private:
    QThreadPool* threadPool;
    std::vector<int>* data;

public:
    ParallelSorter(std::vector<int>* vec) : data(vec) {
        threadPool = new QThreadPool();
        int totalCores = QThread::idealThreadCount();
        int usableCores = std::max(1, totalCores - 1);  // Leave 1 core for OS

        threadPool->setMaxThreadCount(usableCores);

        appendToOutput(QString("System has %1 cores").arg(totalCores));
        appendToOutput(QString("Using %1 threads for sorting").arg(usableCores));
        appendToOutput(QString("Main thread ID: %1").arg((quintptr)QThread::currentThreadId()));
    }

    ~ParallelSorter() {
        delete threadPool;
    }

    void parallelSort() {
        int vectorSize = data->size();
        int numThreads = threadPool->maxThreadCount();
        int chunkSize = vectorSize / numThreads;

        appendToOutput("=== PHASE 1: Sorting chunks in parallel ===");
        appendToOutput(QString("Vector size: %1").arg(vectorSize));
        appendToOutput(QString("Chunk size: %1").arg(chunkSize));

        // Phase 1: Sort chunks in parallel
        for (int i = 0; i < numThreads; i++) {
            int start = i * chunkSize;
            int end = (i == numThreads - 1) ? vectorSize : (i + 1) * chunkSize;

            SortTask* task = new SortTask(data, start, end, i);
            threadPool->start(task);
        }

        // Wait for all sorting tasks to complete
        threadPool->waitForDone();

        appendToOutput("=== PHASE 2: Merging sorted chunks ===");

        // Phase 2: Merge sorted chunks
        // Keep track of current chunk boundaries
        std::vector<std::pair<int, int>> chunks;

        // Initialize with the original chunk boundaries
        for (int i = 0; i < numThreads; i++) {
            int start = i * chunkSize;
            int end = (i == numThreads - 1) ? vectorSize : (i + 1) * chunkSize;
            chunks.push_back({start, end});
        }

        int mergeTaskId = 0;

        // Merge chunks pairwise until only one remains
        while (chunks.size() > 1) {
            std::vector<std::pair<int, int>> newChunks;

            for (size_t i = 0; i < chunks.size(); i += 2) {
                if (i + 1 < chunks.size()) {
                    // Merge two adjacent chunks
                    int start1 = chunks[i].first;
                    int end1 = chunks[i].second;
                    int start2 = chunks[i + 1].first;
                    int end2 = chunks[i + 1].second;

                    MergeTask* mergeTask = new MergeTask(data, start1, end1, start2, end2, mergeTaskId++);
                    threadPool->start(mergeTask);

                    // The merged result spans from start1 to end2
                    newChunks.push_back({start1, end2});
                } else {
                    // Odd chunk out - carry it forward unchanged
                    newChunks.push_back(chunks[i]);
                }
            }

            threadPool->waitForDone();
            chunks = newChunks;
        }

        appendToOutput("=== Sorting complete! ===");
    }
};

// Helper function to verify the vector is sorted
bool isSorted(const std::vector<int>& vec) {
    for (size_t i = 1; i < vec.size(); i++) {
        if (vec[i] < vec[i-1]) {
            return false;
        }
    }
    return true;
}

// Helper function to print first and last elements
void printSample(const std::vector<int>& vec, const QString& label) {
    appendToOutput(label);

    // Print first 10 elements
    QString firstElements = "First 10 elements: ";
    int firstCount = std::min(10, (int)vec.size());
    for (int i = 0; i < firstCount; i++) {
        firstElements += QString::number(vec[i]);
        if (i < firstCount - 1) firstElements += ", ";
    }
    appendToOutput(firstElements);

    // Print last 10 elements
    QString lastElements = "Last 10 elements: ";
    int lastCount = std::min(10, (int)vec.size());
    for (size_t i = vec.size() - lastCount; i < vec.size(); i++) {
        lastElements += QString::number(vec[i]);
        if (i < vec.size() - 1) lastElements += ", ";
    }
    appendToOutput(lastElements);
}

// MainWindow constructor implementation
MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Parallel Sort Demo");
    setFixedSize(800, 600);

    // Set global pointer for thread communication
    g_mainWindow = this;

    // Create layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Create widgets
    statusLabel = new QLabel("Click 'Start Sorting' to begin parallel sort demo");
    statusLabel->setWordWrap(true);

    // Create button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    startButton = new QPushButton("Start Sorting");
    clearButton = new QPushButton("Clear Output");
    buttonLayout->addWidget(startButton);
    buttonLayout->addWidget(clearButton);

    // Create output text area
    outputText = new QTextEdit();
    outputText->setReadOnly(true);
    outputText->setFont(QFont("Courier", 9)); // Monospace font for better alignment

    // Add to layout
    mainLayout->addWidget(statusLabel);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(outputText, 1); // Give text area most of the space

    // Connect buttons
    connect(startButton, &QPushButton::clicked, this, &MainWindow::runSortingDemo);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::clearOutput);

    appendOutput("GUI Application started. Ready to perform parallel sorting!");
}

// MainWindow destructor implementation
MainWindow::~MainWindow() {
    g_mainWindow = nullptr;
}

// Thread-safe method to append text to output
void MainWindow::appendOutput(const QString& text) {
    // Use Qt's queued connection to ensure thread safety
    QMetaObject::invokeMethod(this, [this, text]() {
        outputText->append(text);
        outputText->ensureCursorVisible();
    }, Qt::QueuedConnection);
}

// Clear output slot
void MainWindow::clearOutput() {
    outputText->clear();
    appendOutput("Output cleared. Ready for next sorting demo!");
}

// MainWindow::runSortingDemo implementation
void MainWindow::runSortingDemo() {
    startButton->setEnabled(false);
    statusLabel->setText("Sorting in progress... Watch the output below for details.");

    appendOutput("\n" + QString("=").repeated(60));
    appendOutput("STARTING NEW SORTING DEMO");
    appendOutput(QString("=").repeated(60));

    // Create a vector of random integers
    data.resize(VECTOR_SIZE);

    // Fill with random numbers
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, VECTOR_SIZE);

    appendOutput(QString("Generating %1 random integers...").arg(VECTOR_SIZE));
    for (int i = 0; i < VECTOR_SIZE; i++) {
        data[i] = dis(gen);
    }

    printSample(data, "\nOriginal vector (unsorted):");

    // Time the parallel sort
    QElapsedTimer timer;
    timer.start();

    // Perform parallel sort
    ParallelSorter sorter(&data);
    sorter.parallelSort();

    qint64 parallelTime = timer.elapsed();

    // Verify the result
    bool sorted = isSorted(data);
    appendOutput(QString("\nVector is sorted: %1").arg(sorted ? "true" : "false"));

    printSample(data, "\nSorted vector:");

    appendOutput(QString("\nParallel sort took: %1 ms").arg(parallelTime));

    // For comparison, let's time a single-threaded sort
    appendOutput("\nNow testing single-threaded sort for comparison...");

    // Regenerate random data
    for (int i = 0; i < VECTOR_SIZE; i++) {
        data[i] = dis(gen);
    }

    timer.restart();
    std::sort(data.begin(), data.end());
    qint64 singleThreadTime = timer.elapsed();

    appendOutput(QString("Single-threaded sort took: %1 ms").arg(singleThreadTime));

    double speedup = (double)singleThreadTime / parallelTime;
    appendOutput(QString("Speedup: %1x").arg(speedup, 0, 'f', 2));

    appendOutput(QString("=").repeated(60));
    appendOutput("SORTING DEMO COMPLETE");
    appendOutput(QString("=").repeated(60) + "\n");

    statusLabel->setText("Sorting complete! Results shown above. Click 'Start Sorting' to run again.");
    startButton->setEnabled(true);
}
