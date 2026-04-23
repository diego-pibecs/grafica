#include "physics/assets/CollisionAsset.h"

#include "DebugLog.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <glm/gtc/quaternion.hpp>

namespace
{
constexpr std::size_t kMaxDetailedTrianglesPerAsset = 150000u;

struct JsonValue
{
    enum class Type
    {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object
    };

    using Array = std::vector<JsonValue>;
    using Object = std::unordered_map<std::string, JsonValue>;

    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    Array arrayValue;
    Object objectValue;
};

class JsonParser
{
public:
    explicit JsonParser(std::string source)
        : source_(std::move(source))
    {
    }

    JsonValue Parse()
    {
        SkipWhitespace();
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (position_ != source_.size())
        {
            throw std::runtime_error("Unexpected trailing JSON content");
        }
        return value;
    }

private:
    std::string source_;
    std::size_t position_ = 0;

    char Peek() const
    {
        return position_ < source_.size() ? source_[position_] : '\0';
    }

    char Get()
    {
        return position_ < source_.size() ? source_[position_++] : '\0';
    }

    void Expect(char expected)
    {
        const char current = Get();
        if (current != expected)
        {
            throw std::runtime_error("Invalid JSON syntax");
        }
    }

    void SkipWhitespace()
    {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_])) != 0)
        {
            ++position_;
        }
    }

    JsonValue ParseValue()
    {
        SkipWhitespace();
        const char current = Peek();
        if (current == '{')
        {
            return ParseObject();
        }
        if (current == '[')
        {
            return ParseArray();
        }
        if (current == '"')
        {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.stringValue = ParseString();
            return value;
        }
        if (current == 't' || current == 'f')
        {
            return ParseBoolean();
        }
        if (current == 'n')
        {
            return ParseNull();
        }
        return ParseNumber();
    }

    JsonValue ParseObject()
    {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        Expect('{');
        SkipWhitespace();
        if (Peek() == '}')
        {
            Get();
            return value;
        }

        while (true)
        {
            SkipWhitespace();
            const std::string key = ParseString();
            SkipWhitespace();
            Expect(':');
            SkipWhitespace();
            value.objectValue.emplace(key, ParseValue());
            SkipWhitespace();
            const char separator = Get();
            if (separator == '}')
            {
                break;
            }
            if (separator != ',')
            {
                throw std::runtime_error("Invalid JSON object separator");
            }
        }

        return value;
    }

    JsonValue ParseArray()
    {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        Expect('[');
        SkipWhitespace();
        if (Peek() == ']')
        {
            Get();
            return value;
        }

        while (true)
        {
            value.arrayValue.push_back(ParseValue());
            SkipWhitespace();
            const char separator = Get();
            if (separator == ']')
            {
                break;
            }
            if (separator != ',')
            {
                throw std::runtime_error("Invalid JSON array separator");
            }
            SkipWhitespace();
        }

        return value;
    }

    std::string ParseString()
    {
        Expect('"');
        std::string result;
        while (position_ < source_.size())
        {
            const char current = Get();
            if (current == '"')
            {
                return result;
            }
            if (current == '\\')
            {
                const char escaped = Get();
                switch (escaped)
                {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default:
                    throw std::runtime_error("Unsupported JSON escape sequence");
                }
                continue;
            }

            result.push_back(current);
        }

        throw std::runtime_error("Unterminated JSON string");
    }

    JsonValue ParseBoolean()
    {
        JsonValue value;
        value.type = JsonValue::Type::Boolean;
        if (source_.compare(position_, 4, "true") == 0)
        {
            position_ += 4;
            value.boolValue = true;
            return value;
        }
        if (source_.compare(position_, 5, "false") == 0)
        {
            position_ += 5;
            value.boolValue = false;
            return value;
        }

        throw std::runtime_error("Invalid JSON boolean");
    }

    JsonValue ParseNull()
    {
        if (source_.compare(position_, 4, "null") != 0)
        {
            throw std::runtime_error("Invalid JSON null");
        }

        position_ += 4;
        JsonValue value;
        value.type = JsonValue::Type::Null;
        return value;
    }

    JsonValue ParseNumber()
    {
        const std::size_t start = position_;
        while (position_ < source_.size())
        {
            const char current = source_[position_];
            if (!(std::isdigit(static_cast<unsigned char>(current)) != 0 || current == '-' || current == '+' || current == '.' || current == 'e' || current == 'E'))
            {
                break;
            }
            ++position_;
        }

        if (start == position_)
        {
            throw std::runtime_error("Invalid JSON number");
        }

        JsonValue value;
        value.type = JsonValue::Type::Number;
        const std::string_view token(source_.data() + start, position_ - start);
        value.numberValue = std::stod(std::string(token));
        return value;
    }
};

PhysicsAabb MakeEmptyBounds()
{
    return PhysicsAabb {
        glm::vec3(std::numeric_limits<float>::max()),
        glm::vec3(std::numeric_limits<float>::lowest())
    };
}

void ExpandBounds(PhysicsAabb& bounds, const glm::vec3& point)
{
    bounds.min = glm::min(bounds.min, point);
    bounds.max = glm::max(bounds.max, point);
}

std::string ToLowerAscii(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value;
}

bool ContainsAnyToken(const std::string& lowered, std::initializer_list<std::string_view> tokens)
{
    for (const std::string_view token : tokens)
    {
        if (lowered.find(token) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

glm::vec3 ComputeBoundsSize(const PhysicsAabb& bounds)
{
    return glm::max(bounds.max - bounds.min, glm::vec3(0.0f));
}

glm::vec3 ExtractAxisScale(const glm::mat4& transform)
{
    const glm::vec3 axisX(transform[0]);
    const glm::vec3 axisY(transform[1]);
    const glm::vec3 axisZ(transform[2]);
    return glm::max(
        glm::vec3(glm::length(axisX), glm::length(axisY), glm::length(axisZ)),
        glm::vec3(0.0001f));
}

glm::quat ExtractRotation(const glm::mat4& transform)
{
    glm::mat3 basis(transform);
    const glm::vec3 axisScale = ExtractAxisScale(transform);
    basis[0] /= axisScale.x;
    basis[1] /= axisScale.y;
    basis[2] /= axisScale.z;

    if (glm::determinant(basis) < 0.0f)
    {
        basis[0] *= -1.0f;
    }

    return glm::normalize(glm::quat_cast(basis));
}

glm::vec3 ExtractScaledHalfExtents(const ImportedSubmesh& submesh)
{
    const glm::vec3 localHalfExtents = glm::max((submesh.localBounds.max - submesh.localBounds.min) * 0.5f, glm::vec3(0.025f));
    return glm::max(localHalfExtents * ExtractAxisScale(submesh.worldTransform), glm::vec3(0.025f));
}

glm::vec3 ExtractWorldCenter(const ImportedSubmesh& submesh)
{
    const glm::vec3 localCenter = (submesh.localBounds.min + submesh.localBounds.max) * 0.5f;
    return glm::vec3(submesh.worldTransform * glm::vec4(localCenter, 1.0f));
}

bool MatchesPattern(const ImportedSubmesh& submesh, const std::string& pattern)
{
    const std::string loweredPattern = ToLowerAscii(pattern);
    const std::string loweredName = ToLowerAscii(submesh.name);
    const std::string loweredPath = ToLowerAscii(submesh.nodePath);
    return loweredName.find(loweredPattern) != std::string::npos
        || loweredPath.find(loweredPattern) != std::string::npos;
}

std::optional<std::string> GetStringField(const JsonValue::Object& object, const std::string& key)
{
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->second.type != JsonValue::Type::String)
    {
        return std::nullopt;
    }
    return iterator->second.stringValue;
}

std::optional<double> GetNumberField(const JsonValue::Object& object, const std::string& key)
{
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->second.type != JsonValue::Type::Number)
    {
        return std::nullopt;
    }
    return iterator->second.numberValue;
}

std::optional<bool> GetBoolField(const JsonValue::Object& object, const std::string& key)
{
    const auto iterator = object.find(key);
    if (iterator == object.end() || iterator->second.type != JsonValue::Type::Boolean)
    {
        return std::nullopt;
    }
    return iterator->second.boolValue;
}

std::uint16_t ParseLayerBitsFromString(const std::string& rawValue)
{
    const std::string value = ToLowerAscii(rawValue);
    if (value == "world" || value == "static" || value == "worldstatic")
    {
        return CollisionLayers::WorldStatic;
    }
    if (value == "actor" || value == "player")
    {
        return CollisionLayers::Actor;
    }
    if (value == "trigger")
    {
        return CollisionLayers::Trigger;
    }
    if (value == "dynamic")
    {
        return CollisionLayers::Dynamic;
    }
    if (value == "query")
    {
        return CollisionLayers::Query;
    }
    if (value == "all")
    {
        return CollisionLayers::All;
    }
    return CollisionLayers::None;
}

std::uint16_t ParseMaskField(const JsonValue& value)
{
    if (value.type == JsonValue::Type::Number)
    {
        return static_cast<std::uint16_t>(value.numberValue);
    }

    if (value.type == JsonValue::Type::String)
    {
        return ParseLayerBitsFromString(value.stringValue);
    }

    if (value.type == JsonValue::Type::Array)
    {
        std::uint16_t maskBits = CollisionLayers::None;
        for (const JsonValue& item : value.arrayValue)
        {
            maskBits |= ParseMaskField(item);
        }
        return maskBits;
    }

    return CollisionLayers::All;
}

CollisionSemantic ParseSemantic(const std::string& rawValue)
{
    const std::string value = ToLowerAscii(rawValue);
    if (value == "staticworld" || value == "static")
    {
        return CollisionSemantic::StaticWorld;
    }
    if (value == "dynamicbody" || value == "dynamic")
    {
        return CollisionSemantic::DynamicBody;
    }
    if (value == "trigger")
    {
        return CollisionSemantic::Trigger;
    }
    if (value == "ignore")
    {
        return CollisionSemantic::Ignore;
    }
    return CollisionSemantic::StaticWorld;
}

ColliderBuildMode ParseBuildMode(const std::string& rawValue)
{
    const std::string value = ToLowerAscii(rawValue);
    if (value == "box")
    {
        return ColliderBuildMode::Box;
    }
    if (value == "sphere")
    {
        return ColliderBuildMode::Sphere;
    }
    if (value == "capsule")
    {
        return ColliderBuildMode::Capsule;
    }
    if (value == "convexhull")
    {
        return ColliderBuildMode::ConvexHull;
    }
    if (value == "compound")
    {
        return ColliderBuildMode::Compound;
    }
    return ColliderBuildMode::DetailedTriMesh;
}

CollisionRule MakeFallbackRule(const ImportedSubmesh& submesh, bool preferSimpleStaticShapes)
{
    const std::string lowered = ToLowerAscii(submesh.name + "/" + submesh.nodePath);
    CollisionRule rule;
    rule.pattern = submesh.name;

    if (lowered.find("trigger") != std::string::npos || lowered.find("zone") != std::string::npos)
    {
        rule.semantic = CollisionSemantic::Trigger;
        rule.buildMode = ColliderBuildMode::Box;
        rule.categoryBits = CollisionLayers::Trigger;
        rule.maskBits = CollisionLayers::Actor | CollisionLayers::Query;
        rule.regionId = "triggers";
        return rule;
    }

    if (lowered.find("helper") != std::string::npos
        || lowered.find("socket") != std::string::npos
        || lowered.find("fx") != std::string::npos
        || lowered.find("decal") != std::string::npos
        || lowered.find("light") != std::string::npos)
    {
        rule.semantic = CollisionSemantic::Ignore;
        rule.buildMode = ColliderBuildMode::Box;
        rule.categoryBits = CollisionLayers::None;
        rule.maskBits = CollisionLayers::None;
        rule.regionId = "ignored";
        return rule;
    }

    rule.semantic = CollisionSemantic::StaticWorld;
    rule.buildMode = preferSimpleStaticShapes ? ColliderBuildMode::Box : ColliderBuildMode::DetailedTriMesh;
    rule.categoryBits = CollisionLayers::WorldStatic;
    rule.maskBits = CollisionLayers::Actor | CollisionLayers::Dynamic | CollisionLayers::Query;
    rule.regionId = "static-world";
    rule.enableCharacterQueries = false;
    return rule;
}

bool ShouldPrimitiveBlockCharacter(const ImportedSubmesh& submesh)
{
    const std::string lowered = ToLowerAscii(submesh.name + "/" + submesh.nodePath);
    const glm::vec3 worldSize = ComputeBoundsSize(submesh.worldBounds);
    const glm::vec3 orientedSize = ExtractScaledHalfExtents(submesh) * 2.0f;
    const float minHorizontal = std::min(orientedSize.x, orientedSize.z);
    const float maxHorizontal = std::max(orientedSize.x, orientedSize.z);
    const bool touchesGround = submesh.worldBounds.min.y <= 0.35f;
    const bool floorLike = worldSize.y <= 0.45f && maxHorizontal >= 0.8f;
    const bool wallByName = ContainsAnyToken(lowered, { "wall", "mur", "partition", "frame", "doorframe", "windowframe", "pillar", "column", "fence", "rail" });
    const bool floorByName = ContainsAnyToken(lowered, { "floor", "ground", "terrain", "roof", "ceiling", "stair", "step", "platform", "road", "path", "carpet", "rug", "ramp" });
    const bool furnitureByName = ContainsAnyToken(lowered, {
        "table", "chair", "sofa", "couch", "bed", "desk", "cabinet", "drawer", "wardrobe",
        "fridge", "toilet", "sink", "shelf", "lamp", "plant", "tree", "bush", "dresser",
        "nightstand", "closet", "stool", "bench", "oven", "cooker", "bathtub", "shower"
    });

    if (floorByName || floorLike)
    {
        return false;
    }

    if (wallByName)
    {
        return touchesGround && worldSize.y >= 1.0f;
    }

    if (furnitureByName)
    {
        return touchesGround && (worldSize.y >= 0.25f || maxHorizontal >= 0.35f);
    }

    const bool slabLike = minHorizontal <= 0.35f && maxHorizontal >= 0.8f;
    const bool tallEnough = worldSize.y >= 1.1f;
    const bool bulkyObstacle = touchesGround && worldSize.y >= 0.5f && maxHorizontal >= 0.35f && minHorizontal >= 0.15f;
    return (touchesGround && slabLike && tallEnough) || bulkyObstacle;
}

const CollisionRule* FindRuleForSubmesh(const ImportedSubmesh& submesh, const CollisionProfile& profile)
{
    for (const CollisionRule& rule : profile.rules)
    {
        if (!rule.pattern.empty() && MatchesPattern(submesh, rule.pattern))
        {
            return &rule;
        }
    }
    return nullptr;
}
}

std::filesystem::path GetCollisionProfilePathForAsset(const std::filesystem::path& assetPath)
{
    std::filesystem::path path = assetPath;
    path.replace_extension(".collision.json");
    return path;
}

CollisionProfile LoadCollisionProfile(const std::filesystem::path& profilePath)
{
    CollisionProfile profile;
    if (!std::filesystem::exists(profilePath))
    {
        DebugLog::Info("CollisionAsset", "No collision profile found at ", profilePath.string(), ", using fallback rules");
        return profile;
    }

    DebugLog::Info("CollisionAsset", "Loading collision profile ", profilePath.string());
    std::ifstream stream(profilePath);
    if (!stream.is_open())
    {
        DebugLog::Error("CollisionAsset", "Unable to open collision profile ", profilePath.string());
        throw std::runtime_error("Unable to open collision profile: " + profilePath.string());
    }

    std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    JsonParser parser(std::move(source));
    const JsonValue root = parser.Parse();

    if (root.type != JsonValue::Type::Object)
    {
        DebugLog::Error("CollisionAsset", "Collision profile root is not an object for ", profilePath.string());
        throw std::runtime_error("Collision profile root must be an object");
    }

    const auto rulesIterator = root.objectValue.find("rules");
    if (rulesIterator == root.objectValue.end() || rulesIterator->second.type != JsonValue::Type::Array)
    {
        DebugLog::Info("CollisionAsset", "Collision profile has no rules array at ", profilePath.string());
        return profile;
    }

    for (const JsonValue& ruleValue : rulesIterator->second.arrayValue)
    {
        if (ruleValue.type != JsonValue::Type::Object)
        {
            continue;
        }

        CollisionRule rule;
        if (const std::optional<std::string> pattern = GetStringField(ruleValue.objectValue, "pattern"))
        {
            rule.pattern = *pattern;
        }
        if (const std::optional<std::string> semantic = GetStringField(ruleValue.objectValue, "semantic"))
        {
            rule.semantic = ParseSemantic(*semantic);
        }
        if (const std::optional<std::string> buildMode = GetStringField(ruleValue.objectValue, "buildMode"))
        {
            rule.buildMode = ParseBuildMode(*buildMode);
        }
        if (const std::optional<std::string> regionId = GetStringField(ruleValue.objectValue, "regionId"))
        {
            rule.regionId = *regionId;
        }
        if (const std::optional<bool> enableCharacterQueries = GetBoolField(ruleValue.objectValue, "enableCharacterQueries"))
        {
            rule.enableCharacterQueries = *enableCharacterQueries;
        }

        const auto categoryIterator = ruleValue.objectValue.find("layer");
        if (categoryIterator != ruleValue.objectValue.end())
        {
            rule.categoryBits = ParseMaskField(categoryIterator->second);
        }

        const auto maskIterator = ruleValue.objectValue.find("mask");
        if (maskIterator != ruleValue.objectValue.end())
        {
            rule.maskBits = ParseMaskField(maskIterator->second);
        }

        profile.rules.push_back(std::move(rule));
    }

    DebugLog::Info("CollisionAsset", "Loaded collision profile rules=", profile.rules.size(), " path=", profilePath.string());
    return profile;
}

CollisionAsset BuildCollisionAsset(const ImportedModelAsset& importedAsset, const CollisionProfile& profile)
{
    DebugLog::ScopedTrace trace("CollisionAsset", importedAsset.sourcePath.string());
    CollisionAsset asset;
    asset.sourcePath = importedAsset.sourcePath;
    asset.worldBounds = MakeEmptyBounds();

    std::size_t totalTriangleCount = 0;
    for (const ImportedSubmesh& submesh : importedAsset.submeshes)
    {
        totalTriangleCount += (submesh.indices.size() / 3u);
    }

    const bool preferSimpleStaticShapes = totalTriangleCount > kMaxDetailedTrianglesPerAsset;

    DebugLog::Info(
        "CollisionAsset",
        "Building collision asset source=", importedAsset.sourcePath.string(),
        " submeshes=", importedAsset.submeshes.size(),
        " explicitRules=", profile.rules.size(),
        " totalTriangles=", totalTriangleCount,
        " preferSimpleStaticShapes=", preferSimpleStaticShapes);

    if (preferSimpleStaticShapes)
    {
        DebugLog::Info(
            "CollisionAsset",
            "Large imported asset detected; fallback static colliders will use boxes instead of a full triangle mesh for ",
            importedAsset.sourcePath.string());
    }

    std::unordered_map<std::string, std::size_t> regionIndices;

    for (const ImportedSubmesh& submesh : importedAsset.submeshes)
    {
        const CollisionRule resolvedRule = [&]()
        {
            const CollisionRule* explicitRule = FindRuleForSubmesh(submesh, profile);
            return explicitRule != nullptr ? *explicitRule : MakeFallbackRule(submesh, preferSimpleStaticShapes);
        }();

        if (resolvedRule.semantic == CollisionSemantic::Ignore)
        {
            continue;
        }

        if (resolvedRule.semantic == CollisionSemantic::StaticWorld
            && resolvedRule.buildMode == ColliderBuildMode::DetailedTriMesh)
        {
            const std::string regionId = resolvedRule.regionId.empty() ? "static-world" : resolvedRule.regionId;
            if (regionIndices.find(regionId) == regionIndices.end())
            {
                StaticRegionDesc region;
                region.name = regionId;
                region.regionId = regionId;
                region.bounds = MakeEmptyBounds();
                region.categoryBits = resolvedRule.categoryBits;
                region.maskBits = resolvedRule.maskBits;
                asset.staticRegions.push_back(std::move(region));
                regionIndices.emplace(regionId, asset.staticRegions.size() - 1);
            }

            StaticRegionDesc& region = asset.staticRegions[regionIndices.at(regionId)];
            const std::uint32_t baseVertex = static_cast<std::uint32_t>(region.vertices.size());
            region.vertices.insert(region.vertices.end(), submesh.worldVertices.begin(), submesh.worldVertices.end());
            for (std::uint32_t index : submesh.indices)
            {
                region.indices.push_back(baseVertex + index);
            }
            for (const glm::vec3& vertex : submesh.worldVertices)
            {
                ExpandBounds(region.bounds, vertex);
                ExpandBounds(asset.worldBounds, vertex);
            }
            if (resolvedRule.enableCharacterQueries)
            {
                region.contributesToCharacterQueries = true;
                StaticQueryProxy proxy;
                proxy.name = submesh.name.empty() ? submesh.nodePath : submesh.name;
                proxy.bounds = submesh.worldBounds;
                proxy.center = (submesh.worldBounds.min + submesh.worldBounds.max) * 0.5f;
                proxy.halfExtents = glm::max((submesh.worldBounds.max - submesh.worldBounds.min) * 0.5f, glm::vec3(0.025f));
                proxy.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                proxy.oriented = false;
                proxy.categoryBits = resolvedRule.categoryBits;
                proxy.maskBits = resolvedRule.maskBits;
                region.queryProxies.push_back(std::move(proxy));
            }
            continue;
        }

        if (resolvedRule.semantic == CollisionSemantic::StaticWorld
            && resolvedRule.buildMode == ColliderBuildMode::Box)
        {
            StaticPrimitiveDesc primitive;
            primitive.name = submesh.name.empty() ? submesh.nodePath : submesh.name;
            primitive.bounds = submesh.worldBounds;
            primitive.center = ExtractWorldCenter(submesh);
            primitive.halfExtents = ExtractScaledHalfExtents(submesh);
            primitive.rotation = ExtractRotation(submesh.worldTransform);
            primitive.categoryBits = resolvedRule.categoryBits;
            primitive.maskBits = resolvedRule.maskBits;
            primitive.contributesToCharacterQueries = ShouldPrimitiveBlockCharacter(submesh);
            asset.staticPrimitives.push_back(std::move(primitive));
            ExpandBounds(asset.worldBounds, submesh.worldBounds.min);
            ExpandBounds(asset.worldBounds, submesh.worldBounds.max);
            continue;
        }

        if (resolvedRule.semantic == CollisionSemantic::Trigger)
        {
            TriggerDesc trigger;
            trigger.name = submesh.name;
            trigger.bounds = submesh.worldBounds;
            trigger.categoryBits = resolvedRule.categoryBits;
            trigger.maskBits = resolvedRule.maskBits;
            asset.triggers.push_back(std::move(trigger));
            ExpandBounds(asset.worldBounds, submesh.worldBounds.min);
            ExpandBounds(asset.worldBounds, submesh.worldBounds.max);
            continue;
        }

        DynamicBodyDesc dynamicBody;
        dynamicBody.name = submesh.name;
        dynamicBody.bounds = submesh.worldBounds;
        dynamicBody.categoryBits = resolvedRule.categoryBits;
        dynamicBody.maskBits = resolvedRule.maskBits;
        asset.dynamicBodies.push_back(std::move(dynamicBody));
        ExpandBounds(asset.worldBounds, submesh.worldBounds.min);
        ExpandBounds(asset.worldBounds, submesh.worldBounds.max);
    }

    if (asset.staticRegions.empty() && asset.staticPrimitives.empty() && asset.dynamicBodies.empty() && asset.triggers.empty())
    {
        asset.worldBounds = importedAsset.worldBounds;
    }

    DebugLog::Info(
        "CollisionAsset",
        "Built collision asset source=", importedAsset.sourcePath.string(),
        " staticRegions=", asset.staticRegions.size(),
        " staticPrimitives=", asset.staticPrimitives.size(),
        " queryPrimitives=", std::count_if(
            asset.staticPrimitives.begin(),
            asset.staticPrimitives.end(),
            [](const StaticPrimitiveDesc& primitive) { return primitive.contributesToCharacterQueries; }),
        " dynamicBodies=", asset.dynamicBodies.size(),
        " triggers=", asset.triggers.size());
    return asset;
}
