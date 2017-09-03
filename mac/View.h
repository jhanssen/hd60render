#ifndef VIEW_H
#define VIEW_H

class ViewPrivate;
class Renderer;

class View
{
public:
    View(Renderer* r);
    ~View();

    int exec();

private:
    void init();

private:
    ViewPrivate* mPriv;
    Renderer* mRenderer;
};

#endif
