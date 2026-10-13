// Отключает конфликтующие макросы min и max из Windows.h
#define NOMINMAX
#include <Windows.h>

// FreeGLUT создаёт окно и обрабатывает события, GLU содержит вспомогательные функции
#include <GL/freeglut.h>
#include <GL/glu.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace std;

// Подключает системные библиотеки OpenGL при сборке в Visual Studio
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

const float PI = 3.1415926535f;
const float EPSILON = 0.00001f;

// Двумерная точка исходного сечения
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

// Трёхмерный вектор для координат и направлений
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Точка траектории и угол поворота сечения
struct PathPoint
{
    Vec3 position;
    float rotationAngle = 0.0f;
};

// Доступные типы источников света
enum LightType
{
    DIRECTIONAL_LIGHT,
    POINT_LIGHT,
    SPOT_LIGHT
};

// Доступные процедурные текстуры
enum TextureType
{
    NEUTRAL_TEXTURE,
    CHECKER_TEXTURE,
    METAL_TEXTURE,
    TEXTURE_COUNT
};

// Исходные и вычисленные данные модели
vector<Vec2> Section;
vector<PathPoint> Path;
vector<vector<Vec3>> Rings;
vector<vector<Vec3>> SideNormals;
vector<vector<Vec3>> SmoothNormals;
vector<float> SectionU;
string LoadErrorMessage;

// Параметры модели и окружающей комнаты
Vec3 StartCapNormal;
Vec3 EndCapNormal;
Vec3 ModelCenter;
float ModelRadius = 1.0f;
float ModelMinimumY = 0.0f;
float RoomHalfWidth = 1.0f;
float RoomHalfDepth = 1.0f;
float RoomFloorY = 0.0f;
float RoomCeilingY = 1.0f;

// Размеры окна и ресурсы OpenGL
GLint WindowWidth = 900;
GLint WindowHeight = 700;
GLuint TextureIds[TEXTURE_COUNT] = {};
GLuint RoomDisplayList = 0;

// Текущие режимы отображения
bool WireframeMode = false;
bool ShowNormals = false;
bool TextureEnabled = true;
bool PerspectiveProjection = true;
bool SmoothShading = true;
bool LightingEnabled = true;

LightType CurrentLight = SPOT_LIGHT;
TextureType CurrentTexture = NEUTRAL_TEXTURE;

// Положение камеры в сферических координатах
float CameraYaw = 35.0f;
float CameraPitch = 20.0f;
float CameraDistance = 10.0f;

// Состояние вращения камеры мышью
bool MouseDragging = false;
int LastMouseX = 0;
int LastMouseY = 0;

// Складывает два трёхмерных вектора
Vec3 operator+(const Vec3& first, const Vec3& second)
{
    return {
        first.x + second.x,
        first.y + second.y,
        first.z + second.z
    };
}

// Вычитает один трёхмерный вектор из другого
Vec3 operator-(const Vec3& first, const Vec3& second)
{
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z
    };
}

// Умножает вектор на число
Vec3 operator*(const Vec3& vector, float value)
{
    return {
        vector.x * value,
        vector.y * value,
        vector.z * value
    };
}

// Прибавляет второй вектор к первому
Vec3& operator+=(Vec3& first, const Vec3& second)
{
    first.x += second.x;
    first.y += second.y;
    first.z += second.z;
    return first;
}

// Вычисляет скалярное произведение
float Dot(const Vec3& first, const Vec3& second)
{
    return first.x * second.x +
        first.y * second.y +
        first.z * second.z;
}

// Вычисляет векторное произведение
Vec3 Cross(const Vec3& first, const Vec3& second)
{
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x
    };
}

// Возвращает длину вектора
float Length(const Vec3& vector)
{
    return sqrtf(Dot(vector, vector));
}

// Преобразует вектор в единичный (нормирует)
Vec3 Normalize(const Vec3& vector)
{
    float length = Length(vector);

    if (length < EPSILON)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    return vector * (1.0f / length);
}

// Вычисляет расстояние между двумерными точками
float Distance(const Vec2& first, const Vec2& second)
{
    float dx = second.x - first.x;
    float dy = second.y - first.y;
    return sqrtf(dx * dx + dy * dy);
}

// Считывает сечение и траекторию из файла
bool LoadModelData(const string& fileName)
{
    // Открывает текстовый файл относительно рабочей папки программы
    ifstream file(fileName);

    if (!file)
    {
        LoadErrorMessage = "Cannot open model file: " + fileName;
        return false;
    }

    size_t sectionCount = 0;

    // Первое число задаёт количество вершин плоского сечения
    file >> sectionCount;

    if (!file || sectionCount < 3)
    {
        LoadErrorMessage = "The section must contain at least three vertices.";
        return false;
    }

    // Заранее создаёт нужное число элементов для чтения координат
    Section.resize(sectionCount);

    // Каждая вершина исходного сечения содержит две координаты
    for (size_t i = 0; i < sectionCount; i++)
    {
        file >> Section[i].x >> Section[i].y;
    }

    size_t pathCount = 0;

    // После сечения в файле указано количество точек траектории
    file >> pathCount;

    if (!file || pathCount < 2)
    {
        LoadErrorMessage = "The path must contain at least two points.";
        return false;
    }

    Path.resize(pathCount);

    // Для каждой точки читаются координаты и угол поворота сечения
    for (size_t i = 0; i < pathCount; i++)
    {
        file >> Path[i].position.x
            >> Path[i].position.y
            >> Path[i].position.z
            >> Path[i].rotationAngle;
    }

    // Ошибка потока здесь означает недостающие или неверные значения
    if (!file)
    {
        LoadErrorMessage = "The model file has an invalid format.";
        return false;
    }

    // Знак удвоенной площади показывает направление обхода контура
    float signedArea = 0.0f;

    for (size_t i = 0; i < Section.size(); i++)
    {
        size_t next = (i + 1) % Section.size();
        signedArea += Section[i].x * Section[next].y;
        signedArea -= Section[next].x * Section[i].y;
    }

    // Для единообразного направления внешних нормалей нужен обход против часовой стрелки
    if (signedArea < 0.0f)
    {
        reverse(Section.begin(), Section.end());
    }

    return true;
}

// Возвращает направление траектории в заданной точке
Vec3 GetPathTangent(size_t index)
{
    // В первой точке направление можно определить только по следующей
    if (index == 0)
    {
        return Normalize(Path[1].position - Path[0].position);
    }

    // В последней точке используется направление от предыдущей
    if (index + 1 == Path.size())
    {
        return Normalize(Path[index].position - Path[index - 1].position);
    }

    // Внутри пути направление от предыдущей точки к следующей сглаживает угол ломаной
    return Normalize(Path[index + 1].position - Path[index - 1].position);
}

// Вычисляет текстурную координату вдоль периметра сечения
void BuildSectionTextureCoordinates()
{
    // Для каждой вершины будет храниться положение от 0 до 1 вдоль периметра
    SectionU.assign(Section.size(), 0.0f);
    float perimeter = 0.0f;

    // Накопленная длина задаёт координату вершины на развёртке
    for (size_t i = 1; i < Section.size(); i++)
    {
        perimeter += Distance(Section[i - 1], Section[i]);
        SectionU[i] = perimeter;
    }

    // Добавляет замыкающую сторону от последней вершины к первой
    perimeter += Distance(Section.back(), Section.front());

    if (perimeter < EPSILON)
    {
        return;
    }

    // Деление на периметр переводит накопленные расстояния в диапазон 0..1
    for (size_t i = 0; i < SectionU.size(); i++)
    {
        SectionU[i] /= perimeter;
    }
}

// Строит копии сечения вдоль траектории
void BuildRings()
{
    // Локальные оси задают плоскость каждого сечения
    vector<Vec3> tangents(Path.size());
    vector<Vec3> localX(Path.size());
    vector<Vec3> localY(Path.size());

    // Для каждой точки пути заранее вычисляется направление движения
    for (size_t i = 0; i < Path.size(); i++)
    {
        tangents[i] = GetPathTangent(i);
    }

    // Вспомогательная ось не должна быть почти параллельна касательной
    Vec3 reference = fabsf(Dot(tangents[0], { 0.0f, 1.0f, 0.0f })) < 0.9f
        ? Vec3{ 0.0f, 1.0f, 0.0f }
        : Vec3{ 1.0f, 0.0f, 0.0f };

    // Два векторных произведения строят взаимно перпендикулярные оси сечения
    localX[0] = Normalize(Cross(reference, tangents[0]));
    localY[0] = Normalize(Cross(tangents[0], localX[0]));

    for (size_t i = 1; i < Path.size(); i++)
    {
        // Переносит предыдущую ось в плоскость нового сечения
        Vec3 projected = localX[i - 1] -
            tangents[i] * Dot(localX[i - 1], tangents[i]);

        // При вырожденной проекции ось строится заново через запасное направление
        if (Length(projected) < EPSILON)
        {
            Vec3 fallback = fabsf(Dot(tangents[i], { 0.0f, 1.0f, 0.0f })) < 0.9f
                ? Vec3{ 0.0f, 1.0f, 0.0f }
                : Vec3{ 1.0f, 0.0f, 0.0f };

            projected = Cross(fallback, tangents[i]);
        }

        localX[i] = Normalize(projected);
        localY[i] = Normalize(Cross(tangents[i], localX[i]));
    }

    // Одно кольцо содержит все вершины сечения в одной точке траектории
    Rings.assign(Path.size(), vector<Vec3>(Section.size()));

    for (size_t i = 0; i < Path.size(); i++)
    {
        // Тригонометрические функции принимают угол в радианах
        float angle = Path[i].rotationAngle * PI / 180.0f;
        float cosine = cosf(angle);
        float sine = sinf(angle);

        for (size_t j = 0; j < Section.size(); j++)
        {
            // Сначала вершина поворачивается в двумерной плоскости сечения
            float rotatedX = Section[j].x * cosine - Section[j].y * sine;
            float rotatedY = Section[j].x * sine + Section[j].y * cosine;

            // Локальные координаты переводятся в трёхмерные мировые координаты
            Rings[i][j] = Path[i].position +
                localX[i] * rotatedX +
                localY[i] * rotatedY;
        }
    }

    // Нормали торцов направлены наружу в противоположные стороны траектории
    StartCapNormal = tangents.front() * -1.0f;
    EndCapNormal = tangents.back();
}

// Вычисляет плоские и сглаженные нормали боковых граней
void BuildNormals()
{
    // Сегментов между кольцами на один меньше, чем самих колец
    size_t segmentCount = Rings.size() - 1;
    size_t sideCount = Section.size();

    // Плоские нормали хранятся по граням, сглаженные — по вершинам колец
    SideNormals.assign(segmentCount, vector<Vec3>(sideCount));
    SmoothNormals.assign(Rings.size(), vector<Vec3>(sideCount));

    for (size_t i = 0; i < segmentCount; i++)
    {
        for (size_t j = 0; j < sideCount; j++)
        {
            // Остаток от деления замыкает последнюю сторону на первую вершину
            size_t next = (j + 1) % sideCount;
            Vec3 alongSection = Rings[i][next] - Rings[i][j];
            Vec3 alongPath = Rings[i + 1][j] - Rings[i][j];

            // Векторное произведение двух направлений даёт перпендикуляр к грани
            Vec3 normal = Normalize(Cross(alongSection, alongPath));

            // Нормаль грани добавляется ко всем её вершинам
            SideNormals[i][j] = normal;
            SmoothNormals[i][j] += normal;
            SmoothNormals[i][next] += normal;
            SmoothNormals[i + 1][j] += normal;
            SmoothNormals[i + 1][next] += normal;
        }
    }

    for (size_t i = 0; i < SmoothNormals.size(); i++)
    {
        // Суммы соседних нормалей превращаются в единичные векторы
        for (size_t j = 0; j < SmoothNormals[i].size(); j++)
        {
            SmoothNormals[i][j] = Normalize(SmoothNormals[i][j]);
        }
    }
}

// Вычисляет центр и размер готовой модели
void CalculateModelBounds()
{
    // Начальная вершина служит первым значением минимальных и максимальных границ
    Vec3 minimum = Rings[0][0];
    Vec3 maximum = Rings[0][0];

    // Ищет ограничивающий модель параллелепипед по всем вершинам
    for (const vector<Vec3>& ring : Rings)
    {
        for (const Vec3& point : ring)
        {
            minimum.x = min(minimum.x, point.x);
            minimum.y = min(minimum.y, point.y);
            minimum.z = min(minimum.z, point.z);

            maximum.x = max(maximum.x, point.x);
            maximum.y = max(maximum.y, point.y);
            maximum.z = max(maximum.z, point.z);
        }
    }

    // Центр параллелепипеда используется как цель камеры и света
    ModelCenter = (minimum + maximum) * 0.5f;
    ModelMinimumY = minimum.y;
    ModelRadius = 0.0f;

    // Радиус равен наибольшему расстоянию от центра до вершины
    for (const vector<Vec3>& ring : Rings)
    {
        for (const Vec3& point : ring)
        {
            ModelRadius = max(ModelRadius, Length(point - ModelCenter));
        }
    }

    // Нижняя граница защищает дальнейшие размеры от деления на слишком малое число
    ModelRadius = max(ModelRadius, 1.0f);

    // Размер комнаты и положение пола выбираются относительно размера модели
    RoomHalfWidth = ModelRadius * 5.2f;
    RoomHalfDepth = ModelRadius * 5.2f;
    RoomFloorY = ModelMinimumY - ModelRadius * 0.35f;
    RoomCeilingY = RoomFloorY + ModelRadius * 5.2f;
}

// Последовательно подготавливает геометрию модели
void BuildModel()
{
    BuildSectionTextureCoordinates();
    BuildRings();
    BuildNormals();
    CalculateModelBounds();
    CameraDistance = ModelRadius * 3.0f;
}

// Передаёт процедурную текстуру в OpenGL
void UploadTexture(GLuint textureId, const vector<GLubyte>& pixels, int size)
{
    // Все следующие настройки относятся к выбранному объекту текстуры
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Строки RGB-массива читаются без дополнительного выравнивания байтов
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Линейная фильтрация сглаживает текстуру при изменении её размера на экране
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Координаты за пределами 0..1 повторяют изображение
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // Цвет текстуры умножается на результат освещения поверхности
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // Передаёт массив RGB-пикселей из оперативной памяти в OpenGL
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGB,
        size,
        size,
        0,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );
}

// Создаёт три текстуры без внешних файлов
void CreateTextures()
{
    const int size = 128;

    // На каждый пиксель выделяется по три байта: красный, зелёный и синий
    vector<GLubyte> neutralPixels(size * size * 3);
    vector<GLubyte> checkerPixels(size * size * 3);
    vector<GLubyte> metalPixels(size * size * 3);

    for (int y = 0; y < size; y++)
    {
        for (int x = 0; x < size; x++)
        {
            // Индекс указывает на красную компоненту текущего RGB-пикселя
            int index = (y * size + x) * 3;

            // Детерминированное псевдослучайное число создаёт мелкую неоднородность
            unsigned int noise = static_cast<unsigned int>(
                x * 73856093u ^ y * 19349663u
            );

            // Нейтральная текстура отличается небольшим шумом яркости
            int grain = static_cast<int>(noise % 15u) - 7;
            neutralPixels[index] = static_cast<GLubyte>(174 + grain);
            neutralPixels[index + 1] = static_cast<GLubyte>(176 + grain);
            neutralPixels[index + 2] = static_cast<GLubyte>(179 + grain);

            // Номер клетки по X и Y определяет светлый или тёмный квадрат
            bool lightCell = ((x / 16) + (y / 16)) % 2 == 0;
            checkerPixels[index] = lightCell ? 205 : 92;
            checkerPixels[index + 1] = lightCell ? 210 : 97;
            checkerPixels[index + 2] = lightCell ? 215 : 102;

            // Шум и горизонтальные полосы имитируют шлифованный металл
            int fineGrain = static_cast<int>((noise >> 8) % 11u) - 5;
            int brushLine = static_cast<int>(8.0f * sinf(y * 0.55f));
            int metal = 158 + fineGrain + brushLine;
            metalPixels[index] = static_cast<GLubyte>(metal - 7);
            metalPixels[index + 1] = static_cast<GLubyte>(metal - 2);
            metalPixels[index + 2] = static_cast<GLubyte>(metal + 5);
        }
    }

    // OpenGL создаёт идентификаторы, после чего каждый массив загружается отдельно
    glGenTextures(TEXTURE_COUNT, TextureIds);
    UploadTexture(TextureIds[NEUTRAL_TEXTURE], neutralPixels, size);
    UploadTexture(TextureIds[CHECKER_TEXTURE], checkerPixels, size);
    UploadTexture(TextureIds[METAL_TEXTURE], metalPixels, size);
}

// Задаёт свойства материала трёхмерной модели
void ApplyMaterial()
{
    // Материал задаёт реакцию поверхности на фоновый, рассеянный и зеркальный свет
    const GLfloat ambient[] = { 0.19f, 0.19f, 0.19f, 1.0f };
    const GLfloat diffuse[] = { 0.50f, 0.50f, 0.50f, 1.0f };
    const GLfloat specular[] = { 0.50f, 0.50f, 0.50f, 1.0f };

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    // Чем больше GL_SHININESS, тем меньше и резче зеркальный блик
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 51.0f);
}

// Возвращает положение точечного источника или прожектора
Vec3 GetLightPosition()
{
    // Прожектор вынесен дальше, чтобы его конус охватывал модель и часть комнаты
    if (CurrentLight == SPOT_LIGHT)
    {
        return {
            ModelCenter.x + ModelRadius * 2.65f,
            RoomCeilingY - ModelRadius * 0.22f,
            ModelCenter.z + ModelRadius * 2.65f
        };
    }

    // Точечная лампа находится рядом с моделью со стороны исходного положения камеры
    // Небольшое смещение вверх и в сторону освещает видимые верхние и боковые грани
    return {
        ModelCenter.x + ModelRadius * 1.15f,
        ModelCenter.y + ModelRadius * 1.10f,
        ModelCenter.z + ModelRadius * 1.35f
    };
}

// Настраивает выбранный источник в мировых координатах
void ConfigureLighting()
{
    // Отключение GL_LIGHTING заставляет OpenGL использовать заданные цвета без света
    if (!LightingEnabled)
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_LIGHT0);
        return;
    }

    // Включает общий расчёт освещения и используемый источник GL_LIGHT0
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // Базовые цветовые составляющие источника
    GLfloat ambient[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    GLfloat diffuse[] = { 0.94f, 0.95f, 0.97f, 1.0f };
    GLfloat specular[] = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Для каждого режима корректируется яркость фоновой и прямой составляющих
    if (CurrentLight == DIRECTIONAL_LIGHT)
    {
        ambient[0] = ambient[1] = ambient[2] = 0.45f;
        diffuse[0] = 0.80f;
        diffuse[1] = 0.82f;
        diffuse[2] = 0.86f;
    }
    else if (CurrentLight == POINT_LIGHT)
    {
        // Умеренный фоновый свет смягчает слишком тёмные участки модели
        ambient[0] = 0.10f;
        ambient[1] = 0.105f;
        ambient[2] = 0.115f;
        diffuse[0] = 0.98f;
        diffuse[1] = 0.99f;
        diffuse[2] = 1.0f;
    }
    else
    {
        diffuse[0] = 2.40f;
        diffuse[1] = 2.36f;
        diffuse[2] = 2.28f;
        specular[0] = specular[1] = specular[2] = 2.60f;
    }

    // Передаёт выбранные цветовые составляющие в состояние источника
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    // Ослабление действует только на позиционные источники
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    if (CurrentLight == SPOT_LIGHT)
    {
        glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.012f / ModelRadius);
        glLightf(
            GL_LIGHT0,
            GL_QUADRATIC_ATTENUATION,
            0.0025f / (ModelRadius * ModelRadius)
        );
    }
    else if (CurrentLight == POINT_LIGHT)
    {
        // Более слабое затухание делает расположенную рядом лампу достаточно яркой
        glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.75f);
        glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.045f / ModelRadius);
        glLightf(
            GL_LIGHT0,
            GL_QUADRATIC_ATTENUATION,
            0.012f / (ModelRadius * ModelRadius)
        );
    }
    else
    {
        // Для удалённого направленного источника затухание не применяется
        glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.0f);
        glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.0f);
    }

    if (CurrentLight == DIRECTIONAL_LIGHT)
    {
        // Нулевая координата w задаёт удалённый направленный свет
        const GLfloat direction[] = { -0.45f, 1.0f, 0.35f, 0.0f };
        glLightfv(GL_LIGHT0, GL_POSITION, direction);
        glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180.0f);
        glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 0.0f);
    }
    else
    {
        Vec3 position = GetLightPosition();

        GLfloat lightPosition[] = {
            position.x,
            position.y,
            position.z,
            1.0f
        };

        // Единичная координата w означает источник в конкретной точке мира
        glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);

        if (CurrentLight == SPOT_LIGHT)
        {
            // Прожектор направлен из своей позиции к центру модели
            Vec3 direction = Normalize(ModelCenter - position);
            GLfloat spotDirection[] = {
                direction.x,
                direction.y,
                direction.z
            };

            glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, spotDirection);
            // CUTOFF задаёт половину угла конуса, EXPONENT концентрирует свет к центру
            glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 55.0f);
            glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 12.0f);
        }
        else
        {
            // Угол 180 градусов превращает позиционный источник в точечный
            glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180.0f);
            glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 0.0f);
        }
    }
}

// Передаёт OpenGL нормаль, текстурные координаты и позицию вершины
void EmitVertex(const Vec3& point, const Vec3& normal, float u, float v)
{
    // Нормаль и координаты текстуры становятся атрибутами следующей вершины
    glNormal3f(normal.x, normal.y, normal.z);
    glTexCoord2f(u, v);
    glVertex3f(point.x, point.y, point.z);
}

// Рисует боковые грани и торцы модели
void DrawModel()
{
    // В каркасном режиме текстура не используется
    bool useTexture = TextureEnabled && !WireframeMode;

    // Меняет только способ изображения полигонов, не изменяя их координаты
    glPolygonMode(
        GL_FRONT_AND_BACK,
        WireframeMode ? GL_LINE : GL_FILL
    );

    if (useTexture)
    {
        // Выбирает одну из заранее созданных процедурных текстур
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, TextureIds[CurrentTexture]);
    }
    else
    {
        glDisable(GL_TEXTURE_2D);
    }

    // Материал используется при последующем расчёте освещения модели
    ApplyMaterial();
    glColor3f(0.68f, 0.71f, 0.75f);

    // Сегмент соединяет два соседних кольца, сторона — две соседние вершины
    size_t segmentCount = Rings.size() - 1;
    size_t sideCount = Section.size();

    // Каждая пара соседних сечений соединяется четырёхугольниками
    glBegin(GL_QUADS);

    for (size_t i = 0; i < segmentCount; i++)
    {
        // Координата v постепенно проходит от начала к концу траектории
        float v0 = static_cast<float>(i) / segmentCount;
        float v1 = static_cast<float>(i + 1) / segmentCount;

        for (size_t j = 0; j < sideCount; j++)
        {
            size_t next = (j + 1) % sideCount;
            // Координата u проходит вдоль периметра пятиугольного сечения
            float u0 = SectionU[j];
            float u1 = next == 0 ? 1.0f : SectionU[next];

            // Выбираются усреднённые или общие для грани нормали
            Vec3 firstNormal = SmoothShading
                ? SmoothNormals[i][j]
                : SideNormals[i][j];
            Vec3 secondNormal = SmoothShading
                ? SmoothNormals[i][next]
                : SideNormals[i][j];
            Vec3 thirdNormal = SmoothShading
                ? SmoothNormals[i + 1][next]
                : SideNormals[i][j];
            Vec3 fourthNormal = SmoothShading
                ? SmoothNormals[i + 1][j]
                : SideNormals[i][j];

            EmitVertex(Rings[i][j], firstNormal, u0, v0);
            EmitVertex(Rings[i][next], secondNormal, u1, v0);
            EmitVertex(Rings[i + 1][next], thirdNormal, u1, v1);
            EmitVertex(Rings[i + 1][j], fourthNormal, u0, v1);
        }
    }

    glEnd();

    // Границы сечения нужны для текстурирования торцов
    // Ограничивающий прямоугольник переводит координаты торца в диапазон 0..1
    float minX = Section[0].x;
    float maxX = Section[0].x;
    float minY = Section[0].y;
    float maxY = Section[0].y;

    for (const Vec2& point : Section)
    {
        minX = min(minX, point.x);
        maxX = max(maxX, point.x);
        minY = min(minY, point.y);
        maxY = max(maxY, point.y);
    }

    float width = max(maxX - minX, EPSILON);
    float height = max(maxY - minY, EPSILON);

    // Начальный торец обходится в обратном порядке
    glBegin(GL_POLYGON);
    glNormal3f(StartCapNormal.x, StartCapNormal.y, StartCapNormal.z);

    for (size_t offset = 0; offset < sideCount; offset++)
    {
        size_t j = sideCount - 1 - offset;
        glTexCoord2f(
            (Section[j].x - minX) / width,
            (Section[j].y - minY) / height
        );
        glVertex3f(Rings[0][j].x, Rings[0][j].y, Rings[0][j].z);
    }

    glEnd();

    // Конечный торец обходится в прямом порядке
    glBegin(GL_POLYGON);
    glNormal3f(EndCapNormal.x, EndCapNormal.y, EndCapNormal.z);

    for (size_t j = 0; j < sideCount; j++)
    {
        glTexCoord2f(
            (Section[j].x - minX) / width,
            (Section[j].y - minY) / height
        );
        const Vec3& point = Rings.back()[j];
        glVertex3f(point.x, point.y, point.z);
    }

    glEnd();

    // Возвращает общее состояние, чтобы текстура и каркас не влияли на другие объекты
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Задаёт матовый материал поверхности комнаты
void SetRoomMaterial(float red, float green, float blue)
{
    // Фоновая составляющая немного слабее основного рассеянного цвета
    GLfloat ambient[] = {
        red * 0.65f,
        green * 0.65f,
        blue * 0.65f,
        1.0f
    };
    GLfloat diffuse[] = { red, green, blue, 1.0f };
    // Слабая зеркальная составляющая делает стены почти матовыми
    const GLfloat specular[] = { 0.025f, 0.025f, 0.025f, 1.0f };

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 4.0f);
    glColor3f(red, green, blue);
}

// Делит поверхность комнаты для более точного расчёта света
void DrawRoomSurface(
    const Vec3& origin,
    const Vec3& axisU,
    const Vec3& axisV,
    int divisionsU,
    int divisionsV,
    float red,
    float green,
    float blue
)
{
    // Порядок axisU x axisV задаёт сторону, в которую направлена нормаль
    Vec3 normal = Normalize(Cross(axisU, axisV));
    SetRoomMaterial(red, green, blue);

    glBegin(GL_QUADS);

    for (int u = 0; u < divisionsU; u++)
    {
        // Доли 0..1 выбирают границы текущей ячейки вдоль первой оси
        float u0 = static_cast<float>(u) / divisionsU;
        float u1 = static_cast<float>(u + 1) / divisionsU;

        for (int v = 0; v < divisionsV; v++)
        {
            // Аналогично определяется положение ячейки вдоль второй оси
            float v0 = static_cast<float>(v) / divisionsV;
            float v1 = static_cast<float>(v + 1) / divisionsV;

            // Начало поверхности и два направляющих вектора задают четыре угла ячейки
            Vec3 first = origin + axisU * u0 + axisV * v0;
            Vec3 second = origin + axisU * u1 + axisV * v0;
            Vec3 third = origin + axisU * u1 + axisV * v1;
            Vec3 fourth = origin + axisU * u0 + axisV * v1;

            glNormal3f(normal.x, normal.y, normal.z);
            glVertex3f(first.x, first.y, first.z);
            glVertex3f(second.x, second.y, second.z);
            glVertex3f(third.x, third.y, third.z);
            glVertex3f(fourth.x, fourth.y, fourth.z);
        }
    }

    glEnd();
}

// Рисует замкнутую комнату с нормалями внутрь
void DrawRoom()
{
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Вычисляет мировые координаты шести границ комнаты
    float left = ModelCenter.x - RoomHalfWidth;
    float right = ModelCenter.x + RoomHalfWidth;
    float back = ModelCenter.z - RoomHalfDepth;
    float front = ModelCenter.z + RoomHalfDepth;
    float height = RoomCeilingY - RoomFloorY;

    // Эти векторы задают полную ширину, глубину и высоту поверхностей
    Vec3 horizontalX = { right - left, 0.0f, 0.0f };
    Vec3 horizontalZ = { 0.0f, 0.0f, front - back };
    Vec3 vertical = { 0.0f, height, 0.0f };

    // Пол: Z x X даёт нормаль вверх
    DrawRoomSurface(
        { left, RoomFloorY, back },
        horizontalZ,
        horizontalX,
        80,
        80,
        0.24f,
        0.25f,
        0.27f
    );

    // Потолок: X x Z даёт нормаль вниз
    DrawRoomSurface(
        { left, RoomCeilingY, back },
        horizontalX,
        horizontalZ,
        80,
        80,
        0.39f,
        0.40f,
        0.42f
    );

    // Задняя и передняя стены
    DrawRoomSurface(
        { left, RoomFloorY, back },
        horizontalX,
        vertical,
        80,
        56,
        0.43f,
        0.44f,
        0.46f
    );
    DrawRoomSurface(
        { left, RoomFloorY, front },
        vertical,
        horizontalX,
        56,
        80,
        0.43f,
        0.44f,
        0.46f
    );

    // Левая и правая стены
    DrawRoomSurface(
        { left, RoomFloorY, back },
        vertical,
        horizontalZ,
        56,
        80,
        0.46f,
        0.47f,
        0.49f
    );
    DrawRoomSurface(
        { right, RoomFloorY, back },
        horizontalZ,
        vertical,
        80,
        56,
        0.46f,
        0.47f,
        0.49f
    );
}

// Показывает позицию точечного источника или прожектора
void DrawLightMarker()
{
    // У направленного источника нет конечной позиции для изображения сферы
    if (!LightingEnabled || CurrentLight == DIRECTIONAL_LIGHT)
    {
        return;
    }

    Vec3 position = GetLightPosition();
    const GLfloat emission[] = { 0.95f, 0.96f, 1.0f, 1.0f };
    const GLfloat dark[] = { 0.08f, 0.08f, 0.09f, 1.0f };
    const GLfloat noEmission[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    glDisable(GL_TEXTURE_2D);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, dark);
    // Эмиссия заставляет маркер выглядеть светящимся, но не освещает сцену сама
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);

    // Матрица сохраняется, чтобы перенос сферы не сдвинул следующие объекты
    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);
    glutSolidSphere(ModelRadius * 0.09f, 20, 14);
    glPopMatrix();

    // После сферы свечение материала обязательно отключается
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, noEmission);
}

// Сохраняет неизменяемую геометрию комнаты в списке отображения
void CreateRoomDisplayList()
{
    // Список один раз запоминает команды рисования неизменяемой комнаты
    RoomDisplayList = glGenLists(1);
    glNewList(RoomDisplayList, GL_COMPILE);
    DrawRoom();
    glEndList();
}

// Рисует координатные оси без влияния освещения
void DrawAxes()
{
    // Состояние освещения сохраняется, чтобы восстановить его после цветных линий
    bool lightingWasEnabled = glIsEnabled(GL_LIGHTING) == GL_TRUE;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glLineWidth(2.0f);

    float length = ModelRadius * 1.3f;

    glBegin(GL_LINES);
    glColor3f(0.72f, 0.34f, 0.34f);
    glVertex3f(ModelCenter.x, ModelCenter.y, ModelCenter.z);
    glVertex3f(ModelCenter.x + length, ModelCenter.y, ModelCenter.z);

    glColor3f(0.38f, 0.68f, 0.44f);
    glVertex3f(ModelCenter.x, ModelCenter.y, ModelCenter.z);
    glVertex3f(ModelCenter.x, ModelCenter.y + length, ModelCenter.z);

    glColor3f(0.36f, 0.50f, 0.76f);
    glVertex3f(ModelCenter.x, ModelCenter.y, ModelCenter.z);
    glVertex3f(ModelCenter.x, ModelCenter.y, ModelCenter.z + length);
    glEnd();

    glLineWidth(1.0f);

    if (lightingWasEnabled)
    {
        glEnable(GL_LIGHTING);
    }
}

// Рисует нормали модели цветными отрезками
void DrawNormals()
{
    // Нормали показываются служебными линиями без расчёта освещения
    bool lightingWasEnabled = glIsEnabled(GL_LIGHTING) == GL_TRUE;
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glColor3f(0.58f, 0.78f, 0.92f);

    float length = ModelRadius * 0.12f;
    glBegin(GL_LINES);

    if (SmoothShading)
    {
        // Сглаженные нормали начинаются в вершинах модели
        for (size_t i = 0; i < Rings.size(); i++)
        {
            for (size_t j = 0; j < Rings[i].size(); j++)
            {
                Vec3 end = Rings[i][j] + SmoothNormals[i][j] * length;
                glVertex3f(Rings[i][j].x, Rings[i][j].y, Rings[i][j].z);
                glVertex3f(end.x, end.y, end.z);
            }
        }
    }
    else
    {
        // Плоские нормали начинаются в центрах граней
        for (size_t i = 0; i < SideNormals.size(); i++)
        {
            for (size_t j = 0; j < SideNormals[i].size(); j++)
            {
                size_t next = (j + 1) % Section.size();
                Vec3 center = (
                    Rings[i][j] + Rings[i][next] +
                    Rings[i + 1][next] + Rings[i + 1][j]
                ) * 0.25f;

                Vec3 end = center + SideNormals[i][j] * length;
                glVertex3f(center.x, center.y, center.z);
                glVertex3f(end.x, end.y, end.z);
            }
        }
    }

    Vec3 startCenter;
    Vec3 endCenter;

    // Центры торцов вычисляются как среднее их вершин
    for (size_t j = 0; j < Section.size(); j++)
    {
        startCenter += Rings.front()[j];
        endCenter += Rings.back()[j];
    }

    startCenter = startCenter * (1.0f / Section.size());
    endCenter = endCenter * (1.0f / Section.size());

    Vec3 startEnd = startCenter + StartCapNormal * length;
    Vec3 endEnd = endCenter + EndCapNormal * length;

    glVertex3f(startCenter.x, startCenter.y, startCenter.z);
    glVertex3f(startEnd.x, startEnd.y, startEnd.z);
    glVertex3f(endCenter.x, endCenter.y, endCenter.z);
    glVertex3f(endEnd.x, endEnd.y, endEnd.z);

    glEnd();

    if (lightingWasEnabled)
    {
        glEnable(GL_LIGHTING);
    }
}

// Настраивает перспективную или ортографическую проекцию
void SetupProjection()
{
    // Отношение сторон не даёт изображению растягиваться при изменении окна
    float aspect = static_cast<float>(WindowWidth) / WindowHeight;

    // Все последующие матричные команды изменяют матрицу проекции
    glMatrixMode(GL_PROJECTION);

    // Проекция каждого кадра строится заново без накопления старых значений
    glLoadIdentity();

    if (PerspectiveProjection)
    {
        // Угол обзора 50 градусов создаёт перспективное уменьшение с расстоянием
        gluPerspective(
            50.0,
            aspect,
            0.1,
            CameraDistance + ModelRadius * 10.0f
        );
    }
    else
    {
        // Размер ортографического объёма подбирается под текущее расстояние камеры
        float halfHeight = CameraDistance * tanf(25.0f * PI / 180.0f);
        glOrtho(
            -halfHeight * aspect,
            halfHeight * aspect,
            -halfHeight,
            halfHeight,
            0.1,
            CameraDistance + ModelRadius * 15.0f
        );
    }
}

// Переводит углы и расстояние камеры в трёхмерную позицию
Vec3 GetCameraPosition()
{
    // Стандартные sin и cos работают с радианами, а управление хранит градусы
    float yaw = CameraYaw * PI / 180.0f;
    float pitch = CameraPitch * PI / 180.0f;
    // Это проекция расстояния до камеры на горизонтальную плоскость XZ
    float horizontalDistance = CameraDistance * cosf(pitch);

    // Сферические параметры превращаются в смещение от центра модели
    return ModelCenter + Vec3{
        horizontalDistance * sinf(yaw),
        CameraDistance * sinf(pitch),
        horizontalDistance * cosf(yaw)
    };
}

// Показывает текущие режимы программы в заголовке окна
void UpdateWindowTitle()
{
    string title = "Computer Graphics - Lab 2 | ";
    title += PerspectiveProjection ? "Perspective" : "Orthographic";
    title += SmoothShading ? " | Smooth normals" : " | Flat normals";
    title += WireframeMode ? " | Wireframe" : " | Filled";
    title += LightingEnabled ? " | Light: " : " | Light: Off";

    if (LightingEnabled)
    {
        if (CurrentLight == DIRECTIONAL_LIGHT) title += "Distant directional";
        if (CurrentLight == POINT_LIGHT) title += "Point";
        if (CurrentLight == SPOT_LIGHT) title += "Spot";
    }

    title += TextureEnabled ? " | Texture: " : " | Texture: Off";

    if (TextureEnabled)
    {
        if (CurrentTexture == NEUTRAL_TEXTURE) title += "Neutral";
        if (CurrentTexture == CHECKER_TEXTURE) title += "Checker";
        if (CurrentTexture == METAL_TEXTURE) title += "Brushed metal";
    }

    glutSetWindowTitle(title.c_str());
}

// Обновляет заголовок и запрашивает новый кадр
void RefreshWindow()
{
    UpdateWindowTitle();

    // Запрос помещается в цикл событий, после чего GLUT вызовет Display
    glutPostRedisplay();
}

// Отрисовывает содержимое окна
void Display()
{
    // Очищает цветовой буфер и буфер глубины
    glClearColor(0.015f, 0.017f, 0.022f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // выбираем матрицу проекции и пересоздаём её
    SetupProjection();

    // Переключается матрица камеры и объектов и сбрасывается состояние прошлого кадра
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Камера всегда направлена в центр модели
    Vec3 camera = GetCameraPosition();
    // где наблюдатель, куда смотрит, где верх
    gluLookAt(
        camera.x, camera.y, camera.z,
        ModelCenter.x, ModelCenter.y, ModelCenter.z,
        0.0, 1.0, 0.0
    );

    // Элементы сцены рисуются от комнаты к модели
    // Свет задаётся после камеры, поэтому остаётся закреплённым в мировых координатах
    ConfigureLighting();
    glCallList(RoomDisplayList);
    DrawLightMarker();
    DrawAxes();
    DrawModel();

    if (ShowNormals)
    {
        DrawNormals();
    }

    // Полностью готовый задний буфер становится видимым
    glutSwapBuffers();
}

// Обновляет размеры окна и область вывода OpenGL
void Reshape(GLint width, GLint height)
{
    // Минимум 1 защищает расчёт отношения сторон от деления на ноль
    WindowWidth = max(width, 1);
    WindowHeight = max(height, 1);
    glViewport(0, 0, WindowWidth, WindowHeight);
}

// Возвращает камеру в исходное положение
void ResetCamera()
{
    CameraYaw = 35.0f;
    CameraPitch = 20.0f;
    CameraDistance = ModelRadius * 3.0f;
}

// Приближает или отдаляет камеру в допустимых пределах
void ChangeCameraDistance(float factor)
{
    // Коэффициент меньше 1 приближает, больше 1 отдаляет камеру
    CameraDistance *= factor;

    // Ограничения не дают камере попасть внутрь модели или слишком далеко уйти
    CameraDistance = max(ModelRadius * 1.3f, CameraDistance);
    CameraDistance = min(ModelRadius * 4.8f, CameraDistance);
}

// Обрабатывает обычные клавиши
void Keyboard(unsigned char key, int, int)
{
    switch (key)
    {
    case 27:
        glutLeaveMainLoop();
        return;
    case 'f': case 'F': WireframeMode = !WireframeMode; break;
    case 'n': case 'N': ShowNormals = !ShowNormals; break;
    case 't': case 'T': TextureEnabled = !TextureEnabled; break;
    case 'p': case 'P': PerspectiveProjection = !PerspectiveProjection; break;
    case 's': case 'S': SmoothShading = !SmoothShading; break;
    case 'l': case 'L': LightingEnabled = !LightingEnabled; break;
    case '1': CurrentLight = DIRECTIONAL_LIGHT; LightingEnabled = true; break;
    case '2': CurrentLight = POINT_LIGHT; LightingEnabled = true; break;
    case '3': CurrentLight = SPOT_LIGHT; LightingEnabled = true; break;
    case '4': CurrentTexture = NEUTRAL_TEXTURE; TextureEnabled = true; break;
    case '5': CurrentTexture = CHECKER_TEXTURE; TextureEnabled = true; break;
    case '6': CurrentTexture = METAL_TEXTURE; TextureEnabled = true; break;
    case 'r': case 'R': ResetCamera(); break;
    }

    RefreshWindow();
}

// Обрабатывает стрелки и служебные клавиши
void SpecialKeyboard(int key, int, int)
{
    switch (key)
    {
    case GLUT_KEY_LEFT: CameraYaw -= 5.0f; break;
    case GLUT_KEY_RIGHT: CameraYaw += 5.0f; break;
    case GLUT_KEY_UP: CameraPitch = min(75.0f, CameraPitch + 5.0f); break;
    case GLUT_KEY_DOWN: CameraPitch = max(-8.0f, CameraPitch - 5.0f); break;
    case GLUT_KEY_PAGE_UP: ChangeCameraDistance(0.9f); break;
    case GLUT_KEY_PAGE_DOWN: ChangeCameraDistance(1.1f); break;
    case GLUT_KEY_HOME: ResetCamera(); break;
    }

    RefreshWindow();
}

// Обрабатывает кнопки и колесо мыши
void Mouse(int button, int state, int x, int y)
{
    // ЛКМ включает режим вращения и запоминает начальную позицию курсора
    if (button == GLUT_LEFT_BUTTON)
    {
        MouseDragging = state == GLUT_DOWN;
        LastMouseX = x;
        LastMouseY = y;
    }

    // FreeGLUT сообщает прокрутку вверх как кнопку 3, вниз как кнопку 4
    if (state == GLUT_DOWN && button == 3)
    {
        ChangeCameraDistance(0.9f);
        RefreshWindow();
    }

    if (state == GLUT_DOWN && button == 4)
    {
        ChangeCameraDistance(1.1f);
        RefreshWindow();
    }
}

// Поворачивает камеру при перемещении мыши с зажатой ЛКМ
void MouseMotion(int x, int y)
{
    if (!MouseDragging)
    {
        return;
    }

    // Разница между текущей и прошлой позицией превращается в изменение углов
    CameraYaw += (x - LastMouseX) * 0.4f;
    CameraPitch += (LastMouseY - y) * 0.4f;
    CameraPitch = max(-8.0f, min(75.0f, CameraPitch));

    LastMouseX = x;
    LastMouseY = y;
    RefreshWindow();
}

// Команды контекстного меню
enum MenuCommand
{
    MENU_WIREFRAME = 1,
    MENU_NORMALS,
    MENU_TOGGLE_TEXTURE,
    MENU_NEUTRAL_TEXTURE,
    MENU_CHECKER_TEXTURE,
    MENU_METAL_TEXTURE,
    MENU_PROJECTION,
    MENU_SMOOTHING,
    MENU_LIGHTING,
    MENU_DIRECTIONAL_LIGHT,
    MENU_POINT_LIGHT,
    MENU_SPOT_LIGHT,
    MENU_RESET_CAMERA,
    MENU_EXIT
};

// Выполняет выбранную команду контекстного меню
void Menu(int command)
{
    switch (command)
    {
    case MENU_WIREFRAME: WireframeMode = !WireframeMode; break;
    case MENU_NORMALS: ShowNormals = !ShowNormals; break;
    case MENU_TOGGLE_TEXTURE: TextureEnabled = !TextureEnabled; break;
    case MENU_NEUTRAL_TEXTURE:
        CurrentTexture = NEUTRAL_TEXTURE; TextureEnabled = true; break;
    case MENU_CHECKER_TEXTURE:
        CurrentTexture = CHECKER_TEXTURE; TextureEnabled = true; break;
    case MENU_METAL_TEXTURE:
        CurrentTexture = METAL_TEXTURE; TextureEnabled = true; break;
    case MENU_PROJECTION: PerspectiveProjection = !PerspectiveProjection; break;
    case MENU_SMOOTHING: SmoothShading = !SmoothShading; break;
    case MENU_LIGHTING: LightingEnabled = !LightingEnabled; break;
    case MENU_DIRECTIONAL_LIGHT:
        CurrentLight = DIRECTIONAL_LIGHT; LightingEnabled = true; break;
    case MENU_POINT_LIGHT:
        CurrentLight = POINT_LIGHT; LightingEnabled = true; break;
    case MENU_SPOT_LIGHT:
        CurrentLight = SPOT_LIGHT; LightingEnabled = true; break;
    case MENU_RESET_CAMERA: ResetCamera(); break;
    case MENU_EXIT: glutLeaveMainLoop(); return;
    }

    RefreshWindow();
}

// Создаёт контекстное меню и его вложенные разделы
void CreateContextMenu()
{
    // Подменю света и текстур используют тот же callback Menu
    int lightMenu = glutCreateMenu(Menu);
    glutAddMenuEntry("Distant directional light (1)", MENU_DIRECTIONAL_LIGHT);
    glutAddMenuEntry("Point light (2)", MENU_POINT_LIGHT);
    glutAddMenuEntry("Spot light (3)", MENU_SPOT_LIGHT);

    int textureMenu = glutCreateMenu(Menu);
    glutAddMenuEntry("On / off (T)", MENU_TOGGLE_TEXTURE);
    glutAddMenuEntry("Neutral (4)", MENU_NEUTRAL_TEXTURE);
    glutAddMenuEntry("Checker (5)", MENU_CHECKER_TEXTURE);
    glutAddMenuEntry("Brushed metal (6)", MENU_METAL_TEXTURE);

    // Последнее созданное меню становится основным
    glutCreateMenu(Menu);
    glutAddMenuEntry("Filled / wireframe (F)", MENU_WIREFRAME);
    glutAddMenuEntry("Show normals (N)", MENU_NORMALS);
    glutAddSubMenu("Texture", textureMenu);
    glutAddMenuEntry("Perspective / orthographic (P)", MENU_PROJECTION);
    glutAddMenuEntry("Smooth / flat normals (S)", MENU_SMOOTHING);
    glutAddMenuEntry("Lighting (L)", MENU_LIGHTING);
    glutAddSubMenu("Light type", lightMenu);
    glutAddMenuEntry("Reset camera (Home / R)", MENU_RESET_CAMERA);
    glutAddMenuEntry("Exit (Esc)", MENU_EXIT);
    // Основное меню открывается правой кнопкой мыши
    glutAttachMenu(GLUT_RIGHT_BUTTON);
}

// Включает основные возможности OpenGL и создаёт ресурсы сцены
void InitializeOpenGL()
{
    const GLfloat globalAmbient[] = { 0.008f, 0.008f, 0.01f, 1.0f };

    // Буфер глубины оставляет ближайшие к камере фрагменты
    glEnable(GL_DEPTH_TEST);

    // Нормали автоматически приводятся к единичной длине после преобразований
    glEnable(GL_NORMALIZE);

    // Освещение рассчитывается для обеих сторон граней комнаты и модели
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

    // Направление к наблюдателю вычисляется от каждой вершины для точных бликов
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    // Слабый общий свет не даёт неосвещённым поверхностям стать полностью чёрными
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);

    // Цвета освещения плавно интерполируются между вершинами
    glShadeModel(GL_SMOOTH);

    // Ресурсы создаются после появления действующего окна OpenGL
    CreateTextures();
    CreateRoomDisplayList();
}

// Загружает модель, создаёт окно и запускает цикл GLUT
int main(int argc, char** argv)
{
    // Ошибка входного файла выводится до создания окна OpenGL
    if (!LoadModelData("model.txt"))
    {
        MessageBoxA(
            nullptr,
            LoadErrorMessage.c_str(),
            "KG-2: model loading error",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    BuildModel();

    // Инициализирует окно с двойным буфером и буфером глубины
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(WindowWidth, WindowHeight);
    glutCreateWindow("Computer Graphics - Lab 2");

    InitializeOpenGL();

    // Регистрирует callback-функции обработки событий
    glutDisplayFunc(Display);
    glutReshapeFunc(Reshape);
    glutKeyboardFunc(Keyboard);
    glutSpecialFunc(SpecialKeyboard);
    glutMouseFunc(Mouse);
    glutMotionFunc(MouseMotion);

    // Создаёт меню и передаёт управление главному циклу
    CreateContextMenu();
    UpdateWindowTitle();
    glutMainLoop();

    return 0;
}
