#include "OpenFunscripterUtil.h"

#include "EventSystem.h"

#include <filesystem>
#include  "SDL.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "glad/glad.h"

#include "imgui.h"

#include "tinyfiledialogs.h"

bool Util::LoadTextureFromFile(const char* filename, unsigned int* out_texture, int* out_width, int* out_height)
{
	// Load from file
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create a OpenGL texture identifier
	GLuint image_texture;
	glGenTextures(1, &image_texture);
	glBindTexture(GL_TEXTURE_2D, image_texture);

	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Upload pixels into texture
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	stbi_image_free(image_data);

	*out_texture = image_texture;
	*out_width = image_width;
	*out_height = image_height;

	return true;
}

int Util::OpenFileExplorer(const char* path)
{
#if WIN32
	char tmp[1024];
	stbsp_snprintf(tmp, sizeof(tmp), "explorer %s", path);
	return std::system(tmp);
#elif __APPLE__
	LOG_ERROR("Not implemented for this platform.");
#else
	return OpenUrl(path);
#endif
	return 1;
}

int Util::OpenUrl(const char* url)
{
	char tmp[1024];
#if WIN32
	stbsp_snprintf(tmp, sizeof(tmp), "start %s", url);
	return std::system(tmp);
#elif __APPLE__
	LOG_ERROR("Not implemented for this platform.");
#else
	stbsp_snprintf(tmp, sizeof(tmp), "xdg-open %s", url);
	return std::system(tmp);
#endif
	return 1;
}

void Util::Tooltip(const char* tip)
{
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();
		ImGui::Text("%s", tip);
		ImGui::EndTooltip();
	}
}

void Util::OpenFileDialog(const std::string& title, const std::string& path, FileDialogResultHandler&& handler, bool multiple, const std::vector<const char*>& filters, const std::string& filterText) noexcept
{
	struct FileDialogThreadData {
		bool multiple = false;
		std::string title;
		std::string path;
		std::vector<const char*> filters;
		std::string filterText;
		EventSystem::SingleShotEventHandler handler;
	};
	auto thread = [](void* ctx) {
		auto data = (FileDialogThreadData*)ctx;
		if (!std::filesystem::exists(data->path)) {
			data->path = "";
		}

		auto result = tinyfd_openFileDialog(data->title.c_str(), data->path.c_str(), data->filters.size(), data->filters.data(), !data->filterText.empty() ? data->filterText.c_str() : NULL, data->multiple);
		auto dialogResult = new FileDialogResult;
		if (result != nullptr) {
			if (data->multiple) {
				int last = 0;
				int index = 0;
				for (char c : std::string(result)) {
					if (c == '|') {
						dialogResult->files.emplace_back(std::string(result + last, index - last));
						last = index+1;
					}
					index++;
				}
				dialogResult->files.emplace_back(std::string(result + last, index - last));
			}
			else {
				dialogResult->files.emplace_back(result);
			}
		}

		auto eventData = new EventSystem::SingleShotEventData;
		eventData->ctx = dialogResult;
		eventData->handler = data->handler;

		SDL_Event ev{ 0 };
		ev.type = EventSystem::SingleShotEvent;
		ev.user.data1 = eventData;
		SDL_PushEvent(&ev);
		delete data;
		return 0;
	};
	auto threadData = new FileDialogThreadData;
	threadData->handler = [handler](void* ctx) {
		auto result = (FileDialogResult*)ctx;
		handler(*result);
		delete result;
	};
	threadData->filters = filters;
	threadData->filterText = filterText;
	threadData->multiple = multiple;
	threadData->path = path;
	threadData->title = title;
	auto handle = SDL_CreateThread(thread, "OpenFileDialog", threadData);
	SDL_DetachThread(handle);
}

void Util::SaveFileDialog(const std::string& title, const std::string& path, FileDialogResultHandler&& handler, const std::vector<const char*>& filters, const std::string& filterText) noexcept
{
	struct SaveFileDialogThreadData {
		std::string title;
		std::string path;
		std::vector<const char*> filters;
		std::string filterText;
		EventSystem::SingleShotEventHandler handler;
	};
	auto thread = [](void* ctx) -> int32_t {
		auto data = (SaveFileDialogThreadData*)ctx;

		auto dialogPath = std::filesystem::path(data->path);
		if (std::filesystem::is_directory(dialogPath) && !std::filesystem::exists(dialogPath)) {
			data->path = "";
		}
		else {
			auto directory = dialogPath;
			directory.replace_filename("");
			if (!std::filesystem::exists(directory)) {
				data->path = "";
			}
		}

		auto result = tinyfd_saveFileDialog(data->title.c_str(), data->path.c_str(), data->filters.size(), data->filters.data(), !data->filterText.empty() ? data->filterText.c_str() : NULL);
		auto saveDialogResult = new FileDialogResult;
		if (result != nullptr) {
			saveDialogResult->files.emplace_back(result);
		}
		auto eventData = new EventSystem::SingleShotEventData;
		eventData->ctx = saveDialogResult;
		eventData->handler = data->handler;

		SDL_Event ev{ 0 };
		ev.type = EventSystem::SingleShotEvent;
		ev.user.data1 = eventData;
		SDL_PushEvent(&ev);
		delete data;
		return 0;
	};
	auto threadData = new SaveFileDialogThreadData;
	threadData->title = title;
	threadData->path = path;
	threadData->filters = filters;
	threadData->filterText = filterText;
	threadData->handler = [handler](void* ctx) {
		auto result = (FileDialogResult*)ctx;
		handler(*result);
		delete result;
	};
	auto handle = SDL_CreateThread(thread, "SaveFileDialog", threadData);
}

std::string Util::Resource(const std::string& path) noexcept
{
	auto rel = std::filesystem::path(path);
	rel.make_preferred();
	auto base = Util::Basepath();
	return (base / "data" / rel).string();
}
