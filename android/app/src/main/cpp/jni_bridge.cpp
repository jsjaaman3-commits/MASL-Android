#include "masl_mobile_bridge.hpp"

#include "masl/bytecode/bytecode.hpp"

#include <jni.h>

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace {

std::string java_string(JNIEnv* env, jstring value) {
  if (value == nullptr) {
    return {};
  }
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) {
    return {};
  }
  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

void throw_illegal_argument(JNIEnv* env, const std::string& message) {
  jclass exception_class = env->FindClass("java/lang/IllegalArgumentException");
  if (exception_class != nullptr) {
    env->ThrowNew(exception_class, message.c_str());
  }
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_mahdi_masl_MainActivity_nativeVersion(JNIEnv* env, jobject) {
  const std::string value = masl::mobile::version();
  return env->NewStringUTF(value.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mahdi_masl_MainActivity_nativeAnalyze(JNIEnv* env, jobject,
                                               jstring source,
                                               jstring action) {
  try {
    const auto source_text = java_string(env, source);
    const auto action_text = java_string(env, action);
    const auto output = masl::mobile::analyze_source(source_text, action_text);
    return env->NewStringUTF(output.c_str());
  } catch (const std::exception& exception) {
    throw_illegal_argument(env, exception.what());
    return nullptr;
  } catch (...) {
    throw_illegal_argument(env, "Unknown native MASL analysis error.");
    return nullptr;
  }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_mahdi_masl_MainActivity_nativeCompile(JNIEnv* env, jobject,
                                               jstring source) {
  try {
    const auto source_text = java_string(env, source);
    std::string error;
    auto compiled = masl::mobile::compile_source(source_text, error);
    if (!compiled.has_value()) {
      throw_illegal_argument(env, error.empty() ? "MASL compilation failed."
                                               : error);
      return nullptr;
    }
    if (compiled->bytes.size() > static_cast<std::size_t>(0x7fffffff)) {
      throw_illegal_argument(env, "MASB output is too large for Android JNI.");
      return nullptr;
    }
    const auto size = static_cast<jsize>(compiled->bytes.size());
    jbyteArray result = env->NewByteArray(size);
    if (result == nullptr) {
      return nullptr;
    }
    if (size > 0) {
      env->SetByteArrayRegion(
          result, 0, size,
          reinterpret_cast<const jbyte*>(compiled->bytes.data()));
    }
    return result;
  } catch (const std::exception& exception) {
    throw_illegal_argument(env, exception.what());
    return nullptr;
  } catch (...) {
    throw_illegal_argument(env, "Unknown native MASL compilation error.");
    return nullptr;
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mahdi_masl_MainActivity_nativeRun(JNIEnv* env, jobject,
                                           jbyteArray bytecode,
                                           jstring function,
                                           jobjectArray arguments) {
  try {
    if (bytecode == nullptr) {
      throw_illegal_argument(env, "No MASB bytecode was supplied.");
      return nullptr;
    }
    const jsize byte_count = env->GetArrayLength(bytecode);
    if (byte_count <= 0 ||
        static_cast<std::size_t>(byte_count) >
            masl::bytecode::kMaximumFileSize) {
      throw_illegal_argument(env, "MASB file is empty or too large.");
      return nullptr;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(byte_count));
    env->GetByteArrayRegion(bytecode, 0, byte_count,
                            reinterpret_cast<jbyte*>(bytes.data()));
    if (env->ExceptionCheck()) {
      return nullptr;
    }

    std::vector<std::string> values;
    if (arguments != nullptr) {
      const jsize count = env->GetArrayLength(arguments);
      values.reserve(static_cast<std::size_t>(count));
      for (jsize index = 0; index < count; ++index) {
        auto item = static_cast<jstring>(env->GetObjectArrayElement(arguments,
                                                                    index));
        values.push_back(java_string(env, item));
        if (item != nullptr) {
          env->DeleteLocalRef(item);
        }
      }
    }

    const auto function_name = java_string(env, function);
    const auto output = masl::mobile::run_bytecode(bytes, function_name, values);
    return env->NewStringUTF(output.c_str());
  } catch (const std::exception& exception) {
    throw_illegal_argument(env, exception.what());
    return nullptr;
  } catch (...) {
    throw_illegal_argument(env, "Unknown native MASL runtime error.");
    return nullptr;
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mahdi_masl_MainActivity_nativeSimulate(JNIEnv* env, jobject,
                                               jstring engine, jlong work,
                                               jlong threads, jlong seed,
                                               jstring pattern) {
  try {
    if (work < 0 || threads < 0 || seed < 0) {
      throw_illegal_argument(env, "Simulation counts and seed must be non-negative.");
      return nullptr;
    }
    const auto engine_text = java_string(env, engine);
    const auto pattern_text = java_string(env, pattern);
    const auto output = masl::mobile::simulate_hardware(
        engine_text, static_cast<std::uint64_t>(work),
        static_cast<std::uint64_t>(threads),
        static_cast<std::uint64_t>(seed), pattern_text);
    return env->NewStringUTF(output.c_str());
  } catch (const std::exception& exception) {
    throw_illegal_argument(env, exception.what());
    return nullptr;
  } catch (...) {
    throw_illegal_argument(env, "Unknown native MASL simulation error.");
    return nullptr;
  }
}
