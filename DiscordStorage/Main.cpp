﻿#define NOMINMAX
#include <algorithm>
#include <iostream>
#include <locale>
#include <regex>
#include <future>
#include <thread>
#include <CURL/curl/curl.h>
#include <nlohmann/json.hpp>
#include <dpp/dpp.h>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
using namespace std;
using namespace dpp;
namespace fs = std::filesystem;
size_t part_size = 10475274;
string channelId, messageId, createdWebhook, guild_id, category_id, droppedFilePath, filePath;
using json = nlohmann::json;

json parseConfigFile() {
    json configData;
    std::ifstream file("config.json");

    if (file.is_open()) {
        try {
            file >> configData;
        }
        catch (json::parse_error& e) {
            std::cerr << "Error reading config.json: " << e.what() << std::endl;
        }
        file.close();
    }
    else {
        configData = {
            {"BOT_TOKEN", "bot token"},
            {"guild_id", "your guild id"},
            {"category_id", "your storage category id"}
        };

        // Create and save the file
        std::ofstream outfile("config.json");
        if (outfile.is_open()) {
            outfile << configData.dump(4);
            outfile.close();
            std::cout << "config.json file not found, created: config.json" << std::endl;
        }
        else {
            std::cerr << "Error creating config.json!" << std::endl;
        }
    }

    return configData;
}
struct Response {
    std::string data;
};
struct Link {
    int parca_numarasi;
    std::string channelId;
    std::string messageId;
};


// WINDOWS API
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        char filePath[MAX_PATH];
        DragQueryFileA(hDrop, 0, filePath, MAX_PATH);
        DragFinish(hDrop);
        droppedFilePath = filePath;
        PostMessage(hwnd, WM_CLOSE, 0, 0); // pencereyi kapat
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
std::string openDragDropWindow() {
    const char CLASS_NAME[] = "DragDropWindowClass";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, CLASS_NAME, "Drag the file here",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 200,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    DragAcceptFiles(hwnd, TRUE);

    ShowWindow(hwnd, SW_SHOW);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return droppedFilePath;
}
// select a file, with windows api


// JSON
std::pair<std::string, std::string> id_bul(const std::string& json_str) {
    std::string channelId;  // channel_id
    std::string messageId;  // id
    std::string embed_key = "\"embeds\"";

    // JSON verisini parse et
    auto json_response = nlohmann::json::parse(json_str);

    // Channel ID'yi (Oda ID) bulma
    if (json_response.contains("channel_id")) {
        channelId = json_response["channel_id"].get<std::string>();
    }

    // ID'yi (Mesaj ID) bulma
    if (json_response.contains("id")) {
        messageId = json_response["id"].get<std::string>();
    }

    // Eğer embeds varsa, oradan da id ve channel_id'yi alalım
    if (json_response.contains("embeds") && !json_response["embeds"].empty()) {
        for (const auto& embed : json_response["embeds"]) {
            if (embed.contains("id") && embed.contains("channel_id")) {
                // We take the channel_id and id from the embed and keep them separate
                channelId = embed["channel_id"].get<std::string>();
                messageId = embed["id"].get<std::string>();
            }
        }
    }

    // Oda ve Mesaj ID'lerini döndürüyoruz
    return { channelId, messageId };
}
// extract room and message id from file
std::string json_write(int parca_no, const std::string& channelId, const std::string& messageId) {
    // JSON objesi oluştur
    nlohmann::json json_obj;
    json_obj["partNo"] = parca_no;
    json_obj["channelId"] = channelId;
    json_obj["messageId"] = messageId;

    // JSON'u string olarak döndür
    std::string json_string = json_obj.dump();


    return json_string;  // JSON stringini döndür
}
// json write function
std::string get_second_line(const std::string& dosya_adi) {
    std::ifstream file(dosya_adi);
    std::string line;
    int line_number = 0;

    // İkinci satırı okuma
    while (std::getline(file, line)) {
        line_number++;
        if (line_number == 2) {
            return line; // İkinci satırı döndür
        }
    }

    return ""; // İkinci satır yoksa boş döndür
}
// get cloud file name


// LIBCURL
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    Response* response = static_cast<Response*>(userp);
    response->data.append(static_cast<char*>(contents), total_size);
    return total_size;
}
// libcurl 
size_t write_data(void* buf, size_t size, size_t nmemb, FILE* userp) {
    size_t written = fwrite(buf, size, nmemb, userp);
    return written;
}
// libcurl 
void fileUpload(const std::string& webhook_url, const std::string& filePath, const int& parca_no, const std::string& mesaj, const int& silme, const string& linkleridosyasi) {
    CURL* curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (curl) {
        curl_mime* form = NULL;
        curl_mimepart* field = NULL;

        form = curl_mime_init(curl);

        curl_easy_setopt(curl, CURLOPT_URL, webhook_url.c_str());

        field = curl_mime_addpart(form);
        curl_mime_data(field, mesaj.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_name(field, "content");

        field = curl_mime_addpart(form);
        curl_mime_filedata(field, filePath.c_str());
        curl_mime_name(field, "file");

        curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);

        Response response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "File sending error: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            //std::cout << "\033[1;31m" << filePath << " Successfully dispatched!" << std::endl;

            if (silme) {
                if (std::remove(filePath.c_str()) != 0) {
                    std::cerr << "File deletion error: " << filePath << std::endl;
                }
            }

            std::ofstream dosya2("postlog.txt", std::ios::app);
            dosya2 << "Webhook Response: " << response.data << std::endl;
            dosya2.close();
            auto [channelId2, messageId2] = id_bul(response.data);
            if (!messageId2.empty()) {
                if (silme == 1) {
                    std::ofstream dosya2(linkleridosyasi, std::ios::app);
                    dosya2 << json_write(parca_no, channelId2, messageId2) << std::endl;
                    //cout << json_write(parca_no, channelId2, messageId2) << endl;
                    dosya2.close();
                }
                else {
                    cout << json_write(parca_no, channelId2, messageId2) << endl;
                    channelId = channelId2;
                    messageId = messageId2;
                }
            }
            else {
                std::cout << "URL not found. Please check the error in the submissionlog.txt file." << std::endl;
            }
        }

        curl_mime_free(form);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}
// upload file with webhook
int fileDownload(const std::string& url, const std::string& dosya_adi) {
    CURL* curl;
    FILE* fp;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

        if (fopen_s(&fp, dosya_adi.c_str(), "wb") != 0) {
            std::cerr << "The file could not be opened: " << dosya_adi << std::endl;
            return 1;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        fclose(fp);
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
    return 0;
}
// file download
bool check_token(const std::string& token) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "cURL not started" << std::endl;
        return false;
    }

    std::string url = "https://discord.com/api/v10/users/@me";
    Response response;
    CURLcode res;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Authorization: Bot " + token).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);   

    res = curl_easy_perform(curl);

    bool success = false;

    if (res != CURLE_OK) {
        std::cerr << "cURL Hatası: " << curl_easy_strerror(res) << std::endl;
    }
    else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            success = true;
        }
        else {
            std::cerr << "Geçersiz token. HTTP Kod: " << http_code << std::endl;
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return success;
}

// check discord bot token with curl
std::string get_file_url(const std::string& channel_id, const std::string& message_id) {
    json configData = parseConfigFile();
    std::string bot_token = configData["BOT_TOKEN"];
    CURL* curl;
    CURLcode res;
    std::string read_buffer;

    std::string url = "https://discord.com/api/v10/channels/" + channel_id + "/messages/" + message_id;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Disable SSL authentication
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); // Disable host verification

    if (curl) {
        // Header'lar
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Authorization: Bot " + bot_token).c_str());

        // URL ve header'lar ayarlandı
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Yanıtın okunacağı buffer
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

        // İstek gönderiliyor
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            std::cerr << "Curl hata: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            return "";
        }

        try {
            auto json_response = nlohmann::json::parse(read_buffer);
            if (!json_response.empty() && json_response.contains("attachments") && !json_response["attachments"].empty()) {
                std::string file_url = json_response["attachments"][0]["url"];
                curl_easy_cleanup(curl);
                return file_url;
            }
            else {
                cerr << "Callback: " << read_buffer << endl;
                std::cerr << "No file found in the message!" << std::endl;
                curl_easy_cleanup(curl);
                return "";
            }
        }
        catch (const nlohmann::json::exception& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
            curl_easy_cleanup(curl);
            return "";
        }
    }

    curl_easy_cleanup(curl);
    return "";
}
// create download link


//  DPP
void createWebhook(dpp::cluster& bot, const std::string& channel_id, const std::string& name) {
    dpp::webhook wh;
    wh.name = name;
    wh.channel_id = channel_id;

    string webhook_url;

    bot.create_webhook(wh, [&webhook_url](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            std::cerr << "Failed to create webhook: " << callback.get_error().message << std::endl;
        }
        else {
            const dpp::webhook& created_webhook = std::get<dpp::webhook>(callback.value);
            std::cout << "Webhook has been created: " << created_webhook.name << " (ID: " << created_webhook.id << ")" << std::endl;
            std::cout << "Webhook URL : " << created_webhook.url << std::endl;
            createdWebhook = created_webhook.url; // Assign URL to optional variable
        }
        });
}
// create webhook
void createChannel(dpp::cluster& bot, const std::string& channel_name, const std::string& guild_id, const std::string& category_id) {
    dpp::channel channel = dpp::channel()
        .set_name(channel_name)
        .set_guild_id(guild_id);

    if (category_id != "category_id") {
        channel.set_parent_id(category_id); // If there is a category ID, link the channel to it
    }

    bot.channel_create(channel, [&bot, category_id](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            bot.log(dpp::loglevel::ll_error, callback.get_error().message);
            return;
        }

        auto created_channel = callback.get<dpp::channel>();
        std::string odaismi = created_channel.name;
        std::string odaid = std::to_string(created_channel.id);
        std::cout << "Name of the room: " << odaismi << ", ID: " << odaid << std::endl;
        createWebhook(bot, odaid, "File Uploader");
        });
}
// create channel
string get_messages(dpp::cluster& bot, dpp::snowflake channel_id, int64_t limit) {
    std::promise<std::string> promise; // Create a Promise object
    std::future<std::string> future = promise.get_future(); // Create Future

    bot.messages_get(channel_id, 0, 0, 0, limit, [&promise](const dpp::confirmation_callback_t& callback_response) {
        if (callback_response.is_error()) {
            std::cerr << "Error: " << callback_response.get_error().message << std::endl;
            promise.set_value(""); // Return empty string on error
            return;
        }

        auto messages = callback_response.get<dpp::message_map>();
        std::string contents;

        for (const auto& x : messages) {
            contents += x.second.content + '\n';
        }

        promise.set_value(contents); // Return message content
        });

    return future.get(); // Return result after receiving messages
}
// get last message in room


// FILE
std::string getFileHash(const std::string& filename) {
    try {
        std::string command = "CertUtil -hashfile \"" + filename + "\" SHA256";
        SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
        HANDLE hRead = NULL, hWrite = NULL;

        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
            std::cerr << "Error: Failed to create pipe.\n";
            return "";
        }

        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWrite;
        si.hStdError = hWrite;

        if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hWrite);
            CloseHandle(hRead);
            std::cerr << "Error: Failed to run CertUtil.\n";
            return "";
        }

        CloseHandle(hWrite);

        char buffer[256];
        std::string output;
        DWORD bytesRead;

        while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead) {
            buffer[bytesRead] = '\0';
            output += buffer;
        }

        CloseHandle(hRead);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Satır satır ayır ve ikinci satırı al
        std::istringstream stream(output);
        std::string line;
        int lineNo = 0;
        while (std::getline(stream, line)) {
            if (++lineNo == 2) {
                // Satırda boşluk varsa kaldır
                line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
                return line;
            }
        }

        std::cerr << "Error: Could not extract hash from CertUtil output.\n";
        return "";
    }
    catch (const std::exception& e) {
        std::cerr << "Exception occurred while getting file hash: " << e.what() << std::endl;
        return "";
    }
}
// hash calculation
void get_cloud_files(dpp::cluster& bot, std::vector<dpp::snowflake>& cloudFiles, std::promise<void>& promise) {
    cloudFiles.clear();

    bot.channels_get(guild_id, [&bot, &cloudFiles, &promise](const dpp::confirmation_callback_t& callback) {
        if (callback.is_error()) {
            std::cerr << "Error receiving channels: " << callback.get_error().message << std::endl;
            promise.set_exception(std::make_exception_ptr(std::runtime_error("Error receiving channels")));
            return;
        }

        auto channel_map = std::get<dpp::channel_map>(callback.value);
        for (const auto& [id, channel] : channel_map) {
            if (std::to_string(channel.parent_id) == category_id) {
                cloudFiles.push_back(channel.id);
            }
        }

        // İşlem tamamlandığında promise'in set edilmesi
        promise.set_value();
        });
}
// cloud files list
void chooseFile(dpp::cluster& bot, std::string& filePath) {
    std::vector<dpp::snowflake> cloudFiles;
    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    std::thread getFilesThread([&bot, &cloudFiles, &promise]() {
        get_cloud_files(bot, cloudFiles, promise);
        });
    future.get();

    while (true) {
        std::cout << "\033[1;33mAvailable files:\033[0m" << std::endl;

        // Buluttaki dosyalar
        std::cout << "\033[1;34mFiles in the Cloud:\033[0m" << std::endl;
        for (size_t i = 0; i < cloudFiles.size(); ++i) {
            string obj = get_messages(bot, cloudFiles[i], 1);
            // JSON formatında olup olmadığını kontrol et
            try {
                json json_obj = json::parse(obj);
                std::cout << i + 1 << ". " << json_obj["fileName"] << std::endl;
            }
            catch (const json::parse_error& e) {
                // JSON formatında değilse, hata ver ve geç
                std::cout << "\033[1;31mThe file in the cloud is not in JSON format, it is passed...\033[0m" << std::endl;
                continue;  // JSON hatası alırsak bu öğeyi atla
            }
        }

        std::cout << "\033[1;34m-----------------------------------------------------------------------" << std::endl;
        std::cout << "\033[1;33mPlease make your selection (enter number):\033[0m" << std::endl;

        int input;
        std::cin >> input;

        // Selection control
        if (input > 0 && input <= cloudFiles.size()) {
                string obj = get_messages(bot, cloudFiles[input - 1], 1);
                // We expect it to be in JSON format, but let's check this again
                try {
                    json data = json::parse(obj);
                    string channelId = data["channelId"];
                    string messageId = data["messageId"];
                    std::cout << "oda id: " << channelId << " mesaj id: " << messageId << std::endl;
                    string download_link = get_file_url(channelId, messageId);
                    std::string downloadingFile = "downloaded_file"; // Temporary file name

                    // Download file
                    if (fileDownload(download_link, downloadingFile) == 0) {
                        std::cout << "File downloaded successfully: " << downloadingFile << std::endl;

                        // Read the second line and set the file name
                        string newFileName = get_second_line(downloadingFile);
                        if (!newFileName.empty()) {
                            newFileName += ".txt"; // Add file extension
                            rename(downloadingFile.c_str(), newFileName.c_str()); // Change file name
                            filePath = newFileName; // Update the new path of the downloaded file
                            std::cout << "File name changed: " << newFileName << std::endl;
                        }
                        else {
                            std::cout << "\033[1;31mThe second line could not be read. Failed to change file name.\033[0m" << std::endl;
                        }
                    }
                    else {
                        std::cout << "\033[1;31mFile download failed.\033[0m" << std::endl;
                    }
                }
                catch (const json::parse_error& e) {
                    std::cout << "\033[1;31mThe cloud file is not in JSON format, it is being passed...\033[0m" << std::endl;
                    continue;  // JSON hatası alırsak bu dosyayı atla
                }
            break;
        }
        else {
            std::cout << "\033[1;31mInvalid election.\033[0m" << std::endl;
        }
    }
}
// selects a file, makes file_path the name of the selected file
void listCloudFiles(dpp::cluster& bot) {
    std::vector<dpp::snowflake> cloudFiles;
    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    // Dosya listesini asenkron olarak al
    std::thread getFilesThread([&bot, &cloudFiles, &promise]() {
        get_cloud_files(bot, cloudFiles, promise);
        });

    // Dosyaların alınmasını bekle
    std::cout << "\033[1;33mBuluttaki dosyalar aliniyor, lutfen bekleyin...\033[0m" << std::endl;
    future.get();

    if (cloudFiles.empty()) {
        std::cout << "\033[1;31mBulutta gosterilecek dosya bulunamadi.\033[0m" << std::endl;
        return;
    }

    std::cout << "\033[1;32m--- Bulutta Kayitli Dosyalar ---\033[0m" << std::endl;
    for (size_t i = 0; i < cloudFiles.size(); ++i) {
        try {
            string obj = get_messages(bot, cloudFiles[i], 1);
            json json_obj = json::parse(obj);
            // Dosya adını alıp yazdır
            std::cout << "- " << json_obj["fileName"].get<std::string>() << std::endl;
        }
        catch (const json::parse_error& e) {
            // JSON formatında olmayan veya hatalı bir kayıt varsa atla
            std::cerr << "\033[1;31mBuluttaki bir kayit hatali, atlandi...\033[0m" << std::endl;
            continue;
        }
    }
    std::cout << "\033[1;32m-------------------------------\033[0m" << std::endl;
}

//  OTHER
void printProgressBar(int current, int total, int barWidth = 50) {
    float progress = static_cast<float>(current) / total;
    int pos = static_cast<int>(barWidth * progress);
    std::cout << "\r[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "% (" << current << "/" << total << ")";
    std::cout.flush();
    if (current == total) std::cout << std::endl;
}
// progress bar

// MAIN FUNCTION
void splitFileandUpload(const std::string& filePath, dpp::cluster& bot) {
    std::string fileName = fs::path(filePath).filename().string();
    std::string links_txt = filePath + "_links.txt";
    std::string tempDir = "C:\\Users\\Public\\Documents\\discordStorage\\temp\\";

    // Temp klasörü yoksa oluştur
    if (!fs::exists(tempDir)) {
        fs::create_directories(tempDir);
    }

    try {
        int availablePartNumber = 0;
        std::string hash;

        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            std::cerr << "Error: Could not open file: " << filePath << std::endl;
            return;
        }

        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);
        size_t totalParts = static_cast<size_t>((fileSize + part_size - 1) / part_size);

        std::ifstream control(links_txt);
        if (control) {
            std::string lastLine;
            getline(control, lastLine);
            getline(control, lastLine);
            getline(control, hash);
            getline(control, createdWebhook);
            if (hash == getFileHash(filePath)) {
                while (std::getline(control, lastLine)) {
                    try {
                        json json_obj = json::parse(lastLine);
                        availablePartNumber = json_obj["partNo"];
                    }
                    catch (const std::exception& e) {
                        std::cerr << "JSON parsing error in links file: " << e.what() << std::endl;
                        continue;
                    }
                }
            }
            else {
                std::cerr << "Error: A different file with the same name detected. Please delete or move the other file." << std::endl;
                control.close();
                file.close();
                return;
            }
            control.close();
        }
        else {
            createChannel(bot, fileName, guild_id, category_id);
            std::this_thread::sleep_for(std::chrono::seconds(3));

            std::ofstream file2(links_txt, std::ios::app);
            if (file2.is_open()) {
                file2 << totalParts << std::endl;
                file2 << fileName << std::endl;
                file2 << getFileHash(filePath) << std::endl;
                file2 << createdWebhook << std::endl;
                file2.close();
            }
            else {
                std::cerr << "Error: Could not create links file: " << links_txt << std::endl;
                return;
            }
        }

        std::vector<char> buffer(part_size);
        for (size_t i = availablePartNumber; i < totalParts; ++i) {
            size_t currentPartSize = std::min(part_size, fileSize - i * part_size);
            file.read(buffer.data(), currentPartSize);
            std::string partFilename = tempDir + fileName + ".part" + std::to_string(i + 1);

            std::ofstream partFile(partFilename, std::ios::binary);
            partFile.write(buffer.data(), currentPartSize);
            partFile.close();

            std::string mesaj = "File part: " + std::to_string(i + 1);
            fileUpload(createdWebhook, partFilename, i + 1, mesaj, 1, links_txt);

            printProgressBar(i + 1, totalParts);
        }
        file.close();
    }
    catch (const std::exception& e) {
        std::cerr << "\033[1;31mException caught in splitFileandUpload: " << e.what() << "\033[0m" << std::endl;
    }

    string message1 =
        fileName + "'s download links\n"
        "```\n"
        "File Meaning:\n"
        "1. Line : Total Number of Parts\n"
        "Line 2 : File Name\n"
        "Line 3 : Hash Value of File\n"
        "Line 4 : Webhook Link\n"
        "Next Lines : Track Number, Room Id, Message Id\n"
        "```";
    fileUpload(createdWebhook, links_txt, 0, message1, 0, links_txt);

    json message2 = {
        {"channelId", channelId},
        {"fileName", fileName},
        {"messageId", messageId}
    };
    std::string messageString = message2.dump();
    dpp::webhook wh(createdWebhook);
    bot.execute_webhook(wh, dpp::message(messageString));

    cout << "\033[1;34mDon't write a message in the room.\033[0m" << endl;
    cout << "\033[1;33mIf there is a part that is not deleted, it may not be loaded, please check that part and manually\nload it and add the partoda and message id where it should be.\033[0m" << endl;
    cout << "\033[1;33mPlease do not keep this backup as the only backup of the file, but as an alternative.\033[0m" << endl;
    cout << "\033[1;34mAs they say: If your data is not in three places, it does not exist.\033[0m" << endl;
    cout << "\033[1;31m-----------------------------------------------------------------------" << endl;
}
// splits the selected file and sends it
void mergeFiles(dpp::cluster& bot, const std::string& linksFileName) {
    std::ifstream linksFile(linksFileName);
    if (!linksFile) {
        std::cerr << "Error: Could not open links file: " << linksFileName << std::endl;
        return;
    }
    int totalParts = 0;
    std::string hash;
    std::string line;
    std::string targetFileName;
    std::string webhook;
    std::vector<Link> links;
    if (std::getline(linksFile, line)) {
        try {
            totalParts = std::stoi(line);
        }
        catch (const std::exception& e) {
            std::cerr << "Error: Could not read total parts: " << e.what() << std::endl;
            return;
        }
    }
    if (!std::getline(linksFile, targetFileName)) {
        std::cerr << "Error: Could not read target file name!" << std::endl;
        return;
    }
    if (!std::getline(linksFile, hash)) {
        std::cerr << "Error: Could not read hash!" << std::endl;
        return;
    }
    if (!std::getline(linksFile, webhook)) {
        std::cerr << "Error: Could not read webhook!" << std::endl;
        return;
    }
    while (std::getline(linksFile, line)) {
        int partNumber;
        std::string channelId, messageId;
        json json_obj = json::parse(line);
        messageId = json_obj["messageId"];
        channelId = json_obj["channelId"];
        partNumber = json_obj["partNo"];
        links.push_back({ partNumber, channelId, messageId });
    }
    linksFile.close();
    if (links.size() != totalParts) {
        std::cerr << "Error: Number of links does not match total parts!" << std::endl;
        return;
    }
    std::sort(links.begin(), links.end(), [](const Link& a, const Link& b) {
        return a.parca_numarasi < b.parca_numarasi;
        });
    std::ofstream targetFile(targetFileName, std::ios::binary);
    if (!targetFile) {
        std::cerr << "Error: Could not open target file: " << targetFileName << std::endl;
        return;
    }
    int downloadedParts = 0;
    for (const auto& link : links) {
        std::string newFileName = "part" + std::to_string(link.parca_numarasi) + ".txt";
        std::string url = get_file_url(link.channelId, link.messageId);
        fileDownload(url, newFileName);
        downloadedParts++;
        printProgressBar(downloadedParts, totalParts);
    }
    int mergedParts = 0;
    for (const auto& link : links) {
        std::string newFileName = "part" + std::to_string(link.parca_numarasi) + ".txt";
        std::ifstream partFile(newFileName, std::ios::binary);
        if (partFile) {
            targetFile << partFile.rdbuf();
            partFile.close();
            mergedParts++;
            printProgressBar(mergedParts, totalParts);
        }
        else {
            std::cerr << "Error: Could not open part file: " << newFileName << std::endl;
        }
        if (std::remove(newFileName.c_str()) != 0) {
            std::cerr << "Error: Could not delete part file: " << newFileName << std::endl;
        }
    }
    targetFile.close();
    if (getFileHash(targetFileName) != hash) {
        std::cerr << "Error: File hash mismatch! The downloaded file may be corrupted." << std::endl;
    }
}
// downloads and merges selected files and then deletes temporary files

void show_cli_usage() {
    std::cerr << "\033[1;33mCommand Line Usage:\033[0m" << std::endl;
    std::cerr << " " << "DiscordStorage.exe" << " backup <file_path>" << std::endl;
    std::cerr << " " << "DiscordStorage.exe" << " download <source_file_path>" << std::endl;
    std::cerr << " " << "DiscordStorage.exe" << " upload-error-file <webhook_url> <error_file_path>" << std::endl;
    std::cerr << "\n\033[1;33mExamples:\033[0m" << std::endl;
    std::cerr << " " << "DiscordStorage.exe" << " backup \"C:\\Users\\\Kerem\\\Desktop\\\project.zip\"" << std::endl;
    std::cerr << " " << "DiscordStorage.exe" << " download \"backup_links.txt\"" << std::endl;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "Turkish");
    json configData = parseConfigFile();
    if (!check_token(configData["BOT_TOKEN"].get<std::string>())) {
        std::cerr << "Token not verified!" << std::endl;
        return 1;
    }

    dpp::cluster bot(configData["BOT_TOKEN"].get<std::string>(), dpp::i_default_intents | dpp::i_message_content);
    guild_id = configData["guild_id"];
    category_id = configData["category_id"];

    bot.on_ready([&bot](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            dpp::slashcommand ping("ping", "pong!", bot.me.id);
            bot.global_bulk_command_create({ ping });
        }
        bot.set_presence(dpp::presence(dpp::ps_online, dpp::at_game, "Github: KeremKuyucu/DiscordStorageCPP"));
        });

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            event.reply("pong!");
            std::cout << "Pong!" << std::endl;
        }
        });

    std::thread bot_thread([&bot]() {
        bot.start(dpp::st_wait);
        });

    if (argc > 1) {
        std::string command = argv[1];

        if (command == "list") {
            listCloudFiles(bot);
        }
        else if (command == "backup") {
            if (argc < 3) {
                std::cerr << "\033[1;31mError: No file path specified for the ‘backup’ command.\033[0m" << std::endl;
                show_cli_usage();
                return 1;
            }
            filePath = argv[2];
            std::cout << "Yedekleniyor: " << filePath << std::endl;
            splitFileandUpload(filePath, bot);
        }
        else if (command == "download") {
            if (argc < 3) {
                std::cerr << "\033[1;31mError: The source file path for the ‘download’ command was not specified.\033[0m" << std::endl;
                show_cli_usage();
                return 1;
            }
            filePath = argv[2];
            std::cout << "Downloading " << filePath << " using the links in the file." << std::endl;
            mergeFiles(bot, filePath);
            std::cout << "\033[1;32mFile successfully downloaded!\033[0m" << std::endl;
        }
        else if (command == "upload-error-file") {
            if (argc < 4) {
                std::cerr << "\033[1;31mError: No webhook URL and file path specified for the ‘upload-error-file’ command.\033[0m" << std::endl;
                show_cli_usage();
                return 1;
            }
            std::string hataliwebhook = argv[2];
            filePath = argv[3];
            std::cout << "Incorrect file uploading: " << filePath << std::endl;
            fileUpload(hataliwebhook, filePath, 1, filePath + " ", 0, "postlog.txt");
        }
        else {
            std::cerr << "\033[1;31mInvalid command: " << command << "\033[0m" << std::endl;
            show_cli_usage();
            return 1;
        }
    }
    else {
        std::string input = "";
        while (true) {
            cout << "\033[1;31mProgram owner is Kerem Kuyucu.\033[1;33m Github: KeremKuyucu/DiscordStorage\n";
            cout << "\033[1;34m-----------------------------------------------------------------------" << endl;
            cout << "\033[1;33mPlease enter 'Backup', 'Download' or 'upload-error-file':" << endl;
            cout << "1. Backup" << endl;
            cout << "2. Download" << endl;
            cout << "3. upload-error-file" << endl;
            cout << "4. CLI help" << endl;
            cin >> input;
            cin.ignore();

            if (input == "1") {
                // Upload
                cout << "\033[1;34mPlease encrypt sensitive data before uploading for your security.\033[0m" << endl;
                cout << "\033[1;33mPlease select the file you want to upload.\033[0m" << endl;
                filePath = openDragDropWindow();
                cout << filePath << endl;
                if (!filePath.empty()) {
                    splitFileandUpload(filePath, bot);
                }
            }
            else if (input == "2") {
                // Download
                cout << "\033[1;33mSelect file source:\n1: Cloud Save\n2: Local Save\nYour choice: \033[0m";
                string sourceInput;
                getline(cin, sourceInput);

                if (sourceInput == "1") {
                    chooseFile(bot, filePath); 
                }
                else if (sourceInput == "2") {
                    filePath = openDragDropWindow();
                }
                else {
                    cout << "\033[1;31mInvalid choice.\033[0m" << endl;
                }
                if (!filePath.empty()) {
                    mergeFiles(bot, filePath);
                    cout << "\033[1;32mFile downloaded successfully!\033[0m" << endl;
                }
                else {
                    cout << "\033[1;31mFile path is empty. Operation cancelled.\033[0m" << endl;
                }
            }
            else if (input == "3") {
                // Error file upload
                string hataliwebhook;
                cout << "Please enter a webhook link to upload the faulty file: (Please create an error file room and create a webhook link from there.)" << endl;
                getline(cin, hataliwebhook);
                cout << "Please select the incorrect file." << endl;
                cout << "\033[1;33mPlease wait one second.\033[0m" << endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                filePath = openDragDropWindow();
                cout << filePath << endl;
                fileUpload(hataliwebhook, filePath, 1, filePath + " ", 0, "postlog.txt");
            }
            else if (input == "4") {
                show_cli_usage();
            }
            else {
                cout << "\033[1;31mInvalid election.\033[0m" << endl;
                continue; 
            }
        }
    }

    bot_thread.join();
    return 0;
}