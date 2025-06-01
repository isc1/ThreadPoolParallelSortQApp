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
    static const int VECTOR_SIZE = 1000000;
};

#endif // MAINWINDOW_H
