## PC temperatures (sensors) data sending to Google Firebase

## Описание проекта
Приложение для получения/опроса показаний температурных датчиков устройств ПК и отправки их на удаленный сервер. Идея - рассмотреть различные способы получения температур устройств ПК с возможностью собирать данные на сервере.

- Реализованы классы GPUZTemperatures и AIDA64Temperatures для получения информации о температурах ПК соответственно из GPUZ и AIDA64, используя разделяемую память (shared memory).
- Реализован класс FirebaseDBEasyAdapter как "wrapper" над функциями работы с Google Firebase. К проекту подключены библиотеки Google Firebase.
- Демонстрационное приложение в проекте подключается к аккаунту Firebase, опрашивает значения температур ПК и отправляет информацию в NoSQL базу данных.
- Записи в Firebase:

![Image 1](Images/Image1.PNG?raw=true)
