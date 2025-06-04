#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <vector>
#include <QMutex> // For outputMutex member

// Forward declarations
class QLabel;
class QPushButton;
class QTextEdit;
class QThreadPool;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Thread-safe method to append text to the output
    void appendOutput(const QString& text);

    // Public constants that other classes can access
    static const int VECTOR_SIZE = 10000000; // Original 100000000, reduced for quicker demo
    static const int USE_PCT_CORE = 80;      // Use 80% of each core's capacity

    // Constants for Task 2 (String Matrix)
    static const int STRING_MATRIX_ROWS = 5000;
    static const int STRING_MATRIX_COLS = 500;
    static const int STRING_LENGTH = 4;

    // Constants for Task 3 (Decrement Vector)
    static const int DECREMENT_VECTOR_SIZE = 5000000;
    static const int MAX_RANDOM_VALUE_DECREMENT = 50;

private slots:
    void runSortingDemo();
    void runStringMatrixTask();
    void runDecrementTask();
    void clearOutput();

private:
    QLabel* statusLabel;
    QPushButton* startButton;
    QPushButton* startStringMatrixButton;
    QPushButton* startDecrementButton;
    QPushButton* clearButton;
    QTextEdit* outputText;

    std::vector<int> data; // Used by Task 1 (Original Sort) and Task 3 (Decrement)
    std::vector<std::vector<QString>> stringData; // Used by Task 2

    QThreadPool* m_sharedThreadPool;
    QMutex outputMutex; // Although appendOutput uses QMetaObject, having a general purpose one if needed

    // Helper private methods
    void printStringMatrixSample(const QString& label);
    bool verifyAllZero(const std::vector<int>& vec);
};

#endif // MAINWINDOW_H
