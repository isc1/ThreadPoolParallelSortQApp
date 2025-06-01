// === mainwindow.h ===
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <vector>
#include <QMutex>

class QLabel;
class QPushButton;
class QTextEdit;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Thread-safe method to append text to the output
    void appendOutput(const QString& text);

    // Public constants that other classes can access
    static const int VECTOR_SIZE = 100000000;
    static const int USE_PCT_CORE = 80;  // Use 80% of each core's capacity

private slots:
    void runSortingDemo();
    void clearOutput();

private:
    QLabel* statusLabel;
    QPushButton* startButton;
    QPushButton* clearButton;
    QTextEdit* outputText;
    std::vector<int> data;
    QMutex outputMutex;
};

#endif // MAINWINDOW_H
